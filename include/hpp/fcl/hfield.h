/*
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2021, INRIA
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Open Source Robotics Foundation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

/** \author Justin Carpentier */

#ifndef HPP_FCL_HEIGHT_FIELD_H
#define HPP_FCL_HEIGHT_FIELD_H

#include <hpp/fcl/fwd.hh>
#include <hpp/fcl/collision_object.h>
#include <hpp/fcl/BV/BV_node.h>
#include <hpp/fcl/BVH/BVH_internal.h>

#include <boost/integer/integer_log2.hpp>
#include <vector>

namespace hpp
{
namespace fcl
{

/// @addtogroup Construction_Of_HeightField
/// @{

struct HPP_FCL_DLLAPI HFNodeBase
{
  /// @brief An index for first child node or primitive
  /// If the value is positive, it is the index of the first child bv node
  /// If the value is negative, it is -(primitive index + 1)
  /// Zero is not used.
  size_t first_child;
  
  Eigen::DenseIndex x_id, x_size;
  Eigen::DenseIndex y_id, y_size;
  
  /// @brief Default constructor
  HFNodeBase()
  : first_child(0)
  , x_id(-1), x_size(0)
  , y_id(-1), y_size(0)
  {}
  
  /// @brief Comparison operator
  bool operator==(const HFNodeBase & other) const
  {
    return
       first_child == other.first_child
    && x_id == other.x_id
    && x_size == other.x_size
    && y_id == other.y_id
    && y_size == other.y_size;
  }
  
  /// @brief Difference operator
  bool operator!=(const HFNodeBase & other) const
  {
    return !(*this == other);
  }

  /// @brief Whether current node is a leaf node (i.e. contains a primitive index)
  inline bool isLeaf() const { return x_size == 1 && y_size == 1; }

  /// @brief Return the index of the first child. The index is referred to the bounding volume array (i.e. bvs) in BVHModel
  inline size_t leftChild() const { return first_child; }

  /// @brief Return the index of the second child. The index is referred to the bounding volume array (i.e. bvs) in BVHModel
  inline size_t rightChild() const { return first_child + 1; }
  
  inline Eigen::Vector2i leftChildIndexes() const { return Eigen::Vector2i(x_id,y_id); }
  inline Eigen::Vector2i rightChildIndexes() const { return Eigen::Vector2i(x_id + x_size/2, y_id + y_size/2); }
};

template<typename BV>
struct HPP_FCL_DLLAPI HFNode
: public HFNodeBase
{
  typedef HFNodeBase Base;
  
  /// @brief bounding volume storing the geometry
  BV bv;
  
  /// @brief Equality operator
  bool operator==(const HFNode & other) const
  {
    return Base::operator==(other) && bv == other.bv;
  }
  
  /// @brief Difference operator
  bool operator!=(const HFNode & other) const
  {
    return !(*this == other);
  }

  /// @brief Check whether two BVNode collide
  bool overlap(const HFNode& other) const
  {
    return bv.overlap(other.bv);
  }
  /// @brief Check whether two BVNode collide
  bool overlap(const HFNode& other, const CollisionRequest& request,
               FCL_REAL& sqrDistLowerBound) const
  {
    return bv.overlap(other.bv, request, sqrDistLowerBound);
  }

  /// @brief Compute the distance between two BVNode. P1 and P2, if not NULL and the underlying BV supports distance, return the nearest points.
  FCL_REAL distance(const HFNode& other, Vec3f* P1 = NULL, Vec3f* P2 = NULL) const
  {
    return bv.distance(other.bv, P1, P2);
  }

  /// @brief Access to the center of the BV
  Vec3f getCenter() const { return bv.center(); }

  /// @brief Access to the orientation of the BV
  const Matrix3f& getOrientation() const
  {
    static const Matrix3f id3 = Matrix3f::Identity();
    return id3;
  }

  /// \cond
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW
  /// \endcond
};

namespace details
{

  template<typename BV>
  struct UpdateBoundingVolume
  {
    static void run(const Vec3f & pointA, const Vec3f & pointB, BV & bv)
    {
      AABB bv_aabb;
      bv_aabb.update(pointA,pointB);
      convertBV(bv_aabb,bv);
    }
  };

  template<>
  struct UpdateBoundingVolume<AABB>
  {
    static void run(const Vec3f & pointA, const Vec3f & pointB, AABB & bv)
    {
      bv.update(pointA,pointB);
    }
  };

}

/// @brief Data structure depicting a height field given by the base grid dimensions and the elevation along the grid.
/// \tparam BV one of the bounding volume class in \ref Bounding_Volume.
template<typename BV>
class HPP_FCL_DLLAPI HeightField
: public CollisionGeometry
{
public:
  
  typedef CollisionGeometry Base;
  
  typedef HFNode<BV> Node;
  typedef std::vector<Node> BVS;
  
  /// @brief Constructing an empty HeightField
  HeightField()
  : CollisionGeometry()
  , min_height((std::numeric_limits<FCL_REAL>::min)())
  , max_height((std::numeric_limits<FCL_REAL>::max)())
  {}
  
  /// @brief Constructing an HeightField from basic info
  HeightField(const FCL_REAL x_dim,
              const FCL_REAL y_dim,
              const MatrixXf & heights,
              const FCL_REAL min_height = (FCL_REAL)0)
  : CollisionGeometry()
  {
    init(x_dim,y_dim,heights,min_height);
  }

  /// @brief Copy contructor from another HeightField
  ///
  /// \param[in] other to copy.
  ///
  HeightField(const HeightField & other)
  : CollisionGeometry(other)
  , x_dim(other.x_dim)
  , y_dim(other.y_dim)
  , heights(other.heights)
  , min_height(other.min_height)
  , max_height(other.max_height)
  , x_grid(other.x_grid)
  , y_grid(other.y_grid)
  , bvs(other.bvs)
  , num_bvs(other.num_bvs)
  {
    
  }
  
  /// @brief Returns a const reference of the grid along the X direction.
  const VecXf & getXGrid() const { return x_grid; }
  /// @brief Returns a const reference of the grid along the Y direction.
  const VecXf & getYGrid() const { return y_grid; }
  
  /// @brief Returns a const reference of the heights
  const MatrixXf & getHeights() const { return heights; }
  
  /// @brief Returns the dimension of the Height Field along the X direction.
  FCL_REAL getXDim() const { return x_dim; }
  /// @brief Returns the dimension of the Height Field along the Y direction.
  FCL_REAL getYDim() const { return y_dim; }
  
  /// @brief Returns the minimal height value of the Height Field.
  FCL_REAL getMinHeight() const { return min_height; }
  /// @brief Returns the maximal height value of the Height Field.
  FCL_REAL getMaxHeight() const { return max_height; }
  
  virtual HeightField<BV> * clone() const
  {
    return new HeightField(*this);
  }
  
  /// @brief deconstruction, delete mesh data related.
  virtual ~HeightField() {}
  
  /// @brief Compute the AABB for the HeightField, used for broad-phase collision
  void computeLocalAABB()
  {
    Vec3f A(x_grid[0],y_grid[0],min_height);
    Vec3f B(x_grid[x_grid.size()-1],y_grid[y_grid.size()-1],max_height);
    AABB aabb_(A,B);

    aabb_radius = (A-B).norm()/2.;
    aabb_local = aabb_;
  }
  
  /// \brief Comparison operators
  bool operator==(const HeightField & other) const
  {
    bool res = Base::operator==(other);
    if(!res)
      return false;
    
    res |=
       heights == other.heights
    && x_dim == other.x_dim
    && y_dim == other.y_dim
    && min_height == other.min_height
    && max_height == other.max_height
    && x_grid == other.x_grid
    && y_grid == other.y_grid
    && bvs == other.bvs
    && num_bvs == other.num_bvs
    ;
    
    return res;
  }
  
  /// \brief Difference operator
  bool operator!=(const HeightField & other) const
  {
    return !(*this == other);
  }
  
  /// @brief Update Height Field height
  void updateHeights(const MatrixXf & new_heights)
  {
    if(new_heights.rows() != heights.rows() || new_heights.cols() != heights.cols())
      HPP_FCL_THROW_PRETTY("The matrix containing the new heights values does not have the same matrix size as the original one.\n"
                           "\tinput values - rows: " << new_heights.rows() << " - cols: " << new_heights.cols() << "\n" <<
                           "\texpected values - rows: " << heights.rows() << " - cols: " << heights.cols() << "\n"
                           ,std::invalid_argument);
    
    heights = new_heights.cwiseMax(min_height);
    this->max_height = recursiveUpdateHeight(0);
    assert(this->max_height == heights.maxCoeff());
  }
  
protected:
  
  void init(const FCL_REAL x_dim,
            const FCL_REAL y_dim,
            const MatrixXf & heights,
            const FCL_REAL min_height)
  {
    this->x_dim = x_dim;
    this->y_dim = y_dim;
    this->heights = heights.cwiseMax(min_height);
    this->min_height = min_height;
    this->max_height = heights.maxCoeff();
    
    const Eigen::DenseIndex
    NX = heights.cols(),
    NY = heights.rows();
    assert(NX >= 2 && "The number of columns is too small.");
    assert(NY >= 2 && "The number of rows is too small.");
    
    x_grid = VecXf::LinSpaced(NX,-0.5*x_dim,0.5*x_dim);
    y_grid = VecXf::LinSpaced(NY,0.5*y_dim,-0.5*y_dim);
    
    // Allocate BVS
    const size_t num_tot_bvs = (size_t)(NX * NY) - 1 + (size_t)((NX-1) * (NY-1));
    bvs.resize(num_tot_bvs);
    num_bvs = 0;
    
    // Build tree
    buildTree();
  }

  /// @brief Get the object type: it is a HFIELD
  OBJECT_TYPE getObjectType() const { return OT_HFIELD; }

  Vec3f computeCOM() const
  {
    return Vec3f::Zero();
  }

  FCL_REAL computeVolume() const
  {
    return 0;
  }

  Matrix3f computeMomentofInertia() const
  {
    return Matrix3f::Zero();
  }

protected:

  /// @brief Dimensions in meters along X and Y directions
  FCL_REAL x_dim, y_dim;
  
  /// @brief Elevation values in meters of the Height Field
  MatrixXf heights;
  
  /// @brief Minimal height of the Height Field: all values bellow min_height will be discarded.
  FCL_REAL min_height, max_height;
  
  /// @brief Grids along the X and Y directions. Useful for plotting or other related things.
  VecXf x_grid, y_grid;
  
  /// @brief Bounding volume hierarchy
  BVS bvs;
  unsigned int num_bvs;

  /// @brief Build the bounding volume hierarchy
  int buildTree()
  {
    num_bvs = 1;
    const FCL_REAL max_recursive_height = recursiveBuildTree(0, 0, heights.cols()-1, 0, heights.rows()-1);
    assert(max_recursive_height == max_height && "the maximal height is not correct");
    HPP_FCL_UNUSED_VARIABLE(max_recursive_height);
    
    bvs.resize(num_bvs);
    return BVH_OK;
  }
  
  FCL_REAL recursiveUpdateHeight(const size_t bv_id)
  {
    HFNode<BV> & bv_node = bvs[bv_id];
    
    FCL_REAL max_height;
    if(bv_node.isLeaf())
    {
      max_height = heights.block<2,2>(bv_node.y_id,bv_node.x_id).maxCoeff();
    }
    else
    {
      FCL_REAL
      max_left_height = recursiveUpdateHeight(bv_node.leftChild()),
      max_right_height = recursiveUpdateHeight(bv_node.rightChild());
      
      max_height = (std::max)(max_left_height,max_right_height);
    }
    
    const Vec3f pointA(x_grid[bv_node.x_id],y_grid[bv_node.y_id],min_height);
    const Vec3f pointB(x_grid[bv_node.x_id+bv_node.x_size],y_grid[bv_node.y_id+bv_node.y_size],max_height);
    
    details::UpdateBoundingVolume<BV>::run(pointA,pointB,bv_node.bv);
    
    return max_height;
  }
  
  FCL_REAL recursiveBuildTree(const size_t bv_id,
                              const Eigen::DenseIndex x_id, const Eigen::DenseIndex x_size,
                              const Eigen::DenseIndex y_id, const Eigen::DenseIndex y_size)
  {
    assert(x_id < heights.cols() && "x_id is out of bounds");
    assert(y_id < heights.rows() && "y_id is out of bounds");
    assert(x_size >= 0 && y_size >= 0 && "x_size or y_size are not of correct value");
    assert(bv_id < bvs.size() && "bv_id exceeds the vector dimension");
    
    HFNode<BV> & bvnode = bvs[bv_id];
    FCL_REAL max_height;
    if(x_size == 1 && y_size == 1) // don't build any BV for the current child node
    {
      max_height = heights.block<2,2>(y_id,x_id).maxCoeff();
    }
    else
    {
      bvnode.first_child = num_bvs;
      num_bvs += 2;
      
      FCL_REAL max_left_height = min_height, max_right_height = min_height;
      if(x_size >= y_size) // splitting along the X axis
      {
        Eigen::DenseIndex x_size_half = x_size/2;
        if(x_size == 1) x_size_half = 1;
        max_left_height = recursiveBuildTree(bvnode.leftChild(),
                                             x_id, x_size_half,
                                             y_id, y_size);
        
        max_right_height = recursiveBuildTree(bvnode.rightChild(),
                                              x_id + x_size_half, x_size - x_size_half,
                                              y_id, y_size);
      }
      else // splitting along the Y axis
      {
        Eigen::DenseIndex y_size_half = y_size/2;
        if(y_size == 1) y_size_half = 1;
        max_left_height = recursiveBuildTree(bvnode.leftChild(),
                                             x_id, x_size,
                                             y_id, y_size_half);
        
        max_right_height = recursiveBuildTree(bvnode.rightChild(),
                                              x_id, x_size,
                                              y_id + y_size_half, y_size - y_size_half);
      }
      
      max_height = std::max(max_left_height,max_right_height);
    }

//    max_height = std::max(max_height,min_height);
    
    const Vec3f pointA(x_grid[x_id],y_grid[y_id],min_height);
    assert(x_id+x_size < x_grid.size());
    assert(y_id+y_size < y_grid.size());
    const Vec3f pointB(x_grid[x_id+x_size],y_grid[y_id+y_size],max_height);
    
    details::UpdateBoundingVolume<BV>::run(pointA,pointB,bvnode.bv);
    bvnode.x_id = x_id; bvnode.y_id = y_id;
    bvnode.x_size = x_size; bvnode.y_size = y_size;

    return max_height;
  }

public:
  /// @brief Access the bv giving the its index
  const HFNode<BV> & getBV(unsigned int i) const
  {
    if(i >= num_bvs)
      HPP_FCL_THROW_PRETTY("Index out of bounds", std::invalid_argument);
    return bvs[i];
  }

  /// @brief Access the bv giving the its index
  HFNode<BV> & getBV(unsigned int i)
  {
    if(i >= num_bvs)
      HPP_FCL_THROW_PRETTY("Index out of bounds", std::invalid_argument);
    return bvs[i];
  }
  
  /// @brief Get the BV type: default is unknown
  NODE_TYPE getNodeType() const { return BV_UNKNOWN; }

};

/// @brief Specialization of getNodeType() for HeightField with different BV types
template<>
NODE_TYPE HeightField<AABB>::getNodeType() const;

template<>
NODE_TYPE HeightField<OBB>::getNodeType() const;

template<>
NODE_TYPE HeightField<RSS>::getNodeType() const;

template<>
NODE_TYPE HeightField<kIOS>::getNodeType() const;

template<>
NODE_TYPE HeightField<OBBRSS>::getNodeType() const;

template<>
NODE_TYPE HeightField<KDOP<16> >::getNodeType() const;

template<>
NODE_TYPE HeightField<KDOP<18> >::getNodeType() const;

template<>
NODE_TYPE HeightField<KDOP<24> >::getNodeType() const;

/// @}

}

} // namespace hpp

#endif
