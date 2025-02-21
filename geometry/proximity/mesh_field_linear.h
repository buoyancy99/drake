#pragma once

#include <array>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "drake/common/drake_assert.h"
#include "drake/common/drake_copyable.h"
#include "drake/common/eigen_types.h"
#include "drake/common/reset_on_copy.h"
#include "drake/common/sorted_pair.h"
#include "drake/geometry/proximity/mesh_traits.h"
#include "drake/geometry/proximity/surface_mesh.h"
#include "drake/geometry/proximity/volume_mesh.h"

namespace drake {
namespace geometry {

/**
 %MeshFieldLinear represents a continuous piecewise-linear scalar field `f`
 defined on a (triangular or tetrahedral) mesh; the field value changes
 linearly within each element E (triangle or tetrahedron), and the gradient
 ∇f is constant within each element. The field is continuous across adjacent
 elements, but its gradient is discontinuous from one element to the other.

 To represent a piecewise linear field f, we store one field value per vertex
 of the mesh. Each element E (triangle or tetrahedron) has (d+1) vertices,
 where d is the dimension of the element. For triangle, d = 2, and for
 tetrahedron, d = 3.

 On each element E, we define a linear function fᵉ:ℝ³→ℝ using the field values
 at vertices of E. The gradient ∇fᵉ:ℝ³→ℝ³ is a constant map, so we write ∇fᵉ
 for the constant gradient vector on E as well. For a point Q in element E, we
 have:

        f(Q) = fᵉ(Q) for Q ∈ E,
       ∇f(Q) = ∇fᵉ for Q ∈ E.

 Notice that the domain of fᵉ is the entire space of ℝ³, while the domain of f
 is the underlying space of the mesh.

 The following sections are details for interested readers.

 <h3> Barycentric coordinate </h3>

 For a linear triangle or tetrahedron element E in 3-D, we use barycentric
 coordinate:

       (b₀, b₁, b₂)     for triangle,
       (b₀, b₁, b₂, b₃) for tetrahedron,
       ∑bᵢ = 1, bᵢ ≥ 0,

 to identify a point Q that lies in the simplicial element E. The coefficient
 bᵢ is the weight of vertex Vᵉᵢ of the element E, where the index i is a local
 index within the element E, not the global index of the entire mesh. In other
 words, vertex Vᵉᵢ is the iᵗʰ vertex of E, not the iᵗʰ vertex among all vertices
 in the mesh. The point Q in E can be expressed as:

       Q = ∑bᵉᵢ(Q)Vᵉᵢ,

 where we indicate the barycentric coordinate of a point Q on an element E as
 bᵉᵢ(Q).

 <h3> Field value from barycentric coordinates </h3>

 At a point Q in element E, the piecewise linear field f has value:

       f(Q) = fᵉ(Q) = ∑bᵉᵢ(Q)Fᵉᵢ

 where Fᵉᵢ is the field value at the iᵗʰ vertex of element E.

 <h3> Gradient </h3>

 Consider each bᵉᵢ:ℝ³→ℝ as a linear function, its gradient ∇bᵉᵢ:ℝ³→ℝ³ is a
 constant map, and we write ∇bᵉᵢ for the constant gradient vector. The
 gradient of the piecewise linear field f at a point Q in an element E is:

       ∇f(Q) = ∇fᵉ = ∑Fᵉᵢ∇bᵉᵢ.

 <h3> Field value from Cartesian coordinates </h3>

 At a point Q in element E, the piecewise linear field f has value:

       f(Q) = ∇fᵉ⋅Q + fᵉ(0,0,0).

 Notice that (0,0,0) may or may not lie in element E.

 @tparam T  a valid Eigen scalar for field values.
 @tparam MeshType    the type of the meshes: SurfaceMesh or VolumeMesh.
 */
template <class T, class MeshType>
class MeshFieldLinear {
 public:
  DRAKE_DEFAULT_COPY_AND_MOVE_AND_ASSIGN(MeshFieldLinear)

  /** Constructs a MeshFieldLinear.
   @param values  The field value at each vertex of the mesh.
   @param mesh    The mesh M to which this field refers.
   @param calculate_gradient Calculate gradient field when true, default is
                  true. Calculating gradient allows EvaluateCartesian() to
                  evaluate the field directly instead of converting
                  Cartesian coordinates to barycentric coordinates first.
                  If calculate_gradient is false, EvaluateCartesian() will be
                  slower. On the other hand, calculating gradient requires
                  certain quality from mesh elements. If the mesh quality is
                  very poor, calculating gradient may throw.

   You can use the parameter `calculate_gradient` to trade time and space
   of this constructor for speed of EvaluateCartesian().
   For `calculate_gradient` = true (by default), this constructor will take
   longer time to compute and will store one field-gradient vector for each
   element in the mesh, but the interpolation by EvaluateCartesian() will be
   faster because we will use a dot product with the Cartesian coordinates
   directly, instead of solving a linear system to convert Cartesian
   coordinates to barycentric coordinates first. For `calculate_gradient` =
   false, this constructor will be faster and use less memory, but
   EvaluateCartesian() will be slower.

   When `calculate_gradient` = true, EvaluateGradient() on a mesh element
   will be available. Otherwise, EvaluateGradient() will `throw`.

   The following features are independent of the choice of `calculate_gradient`.

   - Evaluating the field at a vertex.
   - Evaluating the field at a user-given barycentric coordinate.

   @note When `calculate_gradient` = true, a poor quality element can cause
   `throw` due to numerical errors in calculating field gradients. A poor
   quality element is defined as having an extremely large aspect ratio
   R=E/h, where E is the longest edge length and h is the shortest height.
   A height of a triangular element is the distance between a vertex and its
   opposite edge. A height of a tetrahedral element is the distance between a
   vertex and its opposite triangular face. For example, an extremely skinny
   triangle has poor quality, and a tetrahedron with four vertices almost
   co-planar also has poor quality. The exact threshold of the acceptable aspect
   ratio depends on many factors including the underlying scalar type and the
   exact shape and size of the element; however, a rough conservative
   estimation is 1e12.

   @pre   The `mesh` is non-null, and the number of entries in `values` is the
          same as the number of vertices of the mesh.
   */
  MeshFieldLinear(std::vector<T>&& values, const MeshType* mesh,
                  bool calculate_gradient = true)
      : mesh_(mesh), values_(std::move(values)) {
    DRAKE_DEMAND(mesh_ != nullptr);
    DRAKE_DEMAND(static_cast<int>(values_.size()) ==
                 this->mesh().num_vertices());
    if (calculate_gradient) {
      CalcGradientField();
      CalcValueAtMeshOriginForAllElements();
      DRAKE_DEMAND(mesh->num_elements() == static_cast<int>(gradients_.size()));
      DRAKE_DEMAND(mesh->num_elements() ==
                   static_cast<int>(values_at_Mo_.size()));
    }
  }

  DRAKE_DEPRECATED("2022-01-01", "This object will no longer store a name")
  MeshFieldLinear(std::string name, std::vector<T>&& values,
                  const MeshType* mesh, bool calculate_gradient = true)
    : MeshFieldLinear(std::move(values), mesh, calculate_gradient) {
    name_ = std::move(name);
  }

  /** Evaluates the field value at a vertex.
   @param v The index of the vertex.
   */
  const T& EvaluateAtVertex(typename MeshType::VertexIndex v) const {
    return values_[v];
  }

  /** Evaluates the field value at a location on an element.

   The return type depends on both the field's scalar type `T` and the
   Barycentric coordinate type `B`.  See
   @ref drake::geometry::promoted_numerical "promoted_numerical_t" for details.

   @param e The index of the element.
   @param b The barycentric coordinates.
   @tparam B The scalar type for the barycentric coordinate.
   */
  template <typename B>
  promoted_numerical_t<B, T> Evaluate(
      typename MeshType::ElementIndex e,
      // NOLINTNEXTLINE(runtime/references): "template Bar..." confuses cpplint.
      const typename MeshType::template Barycentric<B>& b) const {
    const auto& element = this->mesh().element(e);
    promoted_numerical_t<B, T> value = b[0] * values_[element.vertex(0)];
    for (int i = 1; i < MeshType::kVertexPerElement; ++i) {
      value += b[i] * values_[element.vertex(i)];
    }
    return value;
  }

  /** Evaluates the field at a point Qp on an element. If the element is a
   tetrahedron, Qp is the input point Q. If the element is a triangle, Qp is the
   projection of Q on the triangle's plane.

   If gradients have been calculated, it evaluates the field value directly.
   Otherwise, it converts Cartesian coordinates to barycentric coordinates
   for barycentric interpolation.

   The return type depends on both the field's scalar type `T` and the
   Cartesian coordinate type `C`.  See
   @ref drake::geometry::promoted_numerical "promoted_numerical_t" for details.

   @param e The index of the element.
   @param p_MQ The position of point Q expressed in frame M, in Cartesian
               coordinates. M is the frame of the mesh.
   */
  template <typename C>
  promoted_numerical_t<C, T> EvaluateCartesian(
      typename MeshType::ElementIndex e, const Vector3<C>& p_MQ) const {
    if (gradients_.size() == 0) {
      return Evaluate(e, this->mesh().CalcBarycentric(p_MQ, e));
    } else {
      DRAKE_ASSERT(e < gradients_.size());
      DRAKE_ASSERT(e < values_at_Mo_.size());
      return gradients_[e].dot(p_MQ) + values_at_Mo_[e];
    }
  }

  /** Evaluates the gradient in the domain of the element indicated by `e`.
  The gradient is a vector in R³ expressed in frame M. For surface meshes, it
  will particularly lie parallel to the plane of the corresponding triangle.
  @throws std::exception if the gradient vector was not calculated.
  */
  Vector3<T> EvaluateGradient(typename MeshType::ElementIndex e) const {
    if (gradients_.size() == 0) {
      throw std::runtime_error("Gradient vector was not calculated.");
    }
    return gradients_[e];
  }

  /** Transforms the gradient vectors of this field from its initial frame M
   to the new frame N.
   @warning Use this function when the reference mesh of this field changes
   its frame in the same way.
   */
  void TransformGradients(
      const math::RigidTransform<typename MeshType::ScalarType>& X_NM) {
    for (auto& grad : gradients_) {
      grad = X_NM.rotation() * grad;
    }
  }

  /** Copy to a new %MeshFieldLinear and set the new %MeshFieldLinear to use a
   new compatible mesh. %MeshFieldLinear needs a mesh to operate; however,
   %MeshFieldLinear does not own the mesh. In fact, several %MeshFieldLinear
   objects can use the same mesh.  */
  [[nodiscard]] std::unique_ptr<MeshFieldLinear> CloneAndSetMesh(
      const MeshType* new_mesh) const {
    DRAKE_DEMAND(new_mesh != nullptr);
    DRAKE_DEMAND(new_mesh->num_vertices() == mesh_->num_vertices());
    // TODO(DamrongGuoy): Check that the `new_mesh` is equivalent to the
    //  current `mesh_M_`.
    std::unique_ptr<MeshFieldLinear> new_mesh_field = CloneWithNullMesh();
    new_mesh_field->mesh_ = new_mesh;
    return new_mesh_field;
  }

  const MeshType& mesh() const { return *mesh_; }
  DRAKE_DEPRECATED("2022-01-01", "This object will no longer store a name")
  const std::string& name() const { return name_; }
  const std::vector<T>& values() const { return values_; }

  // TODO(#12173): Consider NaN==NaN to be true in equality tests.
  /** Checks to see whether the given MeshFieldLinear object is equal via deep
   exact comparison. The name of the objects are exempt from this comparison.
   NaNs are treated as not equal as per the IEEE standard.
   @param field The field for comparison.
   @returns `true` if the given field is equal.
   */
  bool Equal(const MeshFieldLinear<T, MeshType>& field) const {
    if (!this->mesh().Equal(field.mesh())) return false;

    // Check field value at each vertex.
    for (typename MeshType::VertexIndex i(0); i < this->mesh().num_vertices();
         ++i) {
      if (values_.at(i) != field.values_.at(i)) return false;
    }
    if (gradients_ != field.gradients_) return false;
    if (values_at_Mo_ != field.values_at_Mo_) return false;
    // All checks passed.
    return true;
  }

 private:
  // Clones MeshFieldLinear data under the assumption that the mesh
  // pointer is null.
  [[nodiscard]] std::unique_ptr<MeshFieldLinear<T, MeshType>>
  CloneWithNullMesh() const { return std::make_unique<MeshFieldLinear>(*this); }

  void CalcGradientField() {
    gradients_.clear();
    gradients_.reserve(this->mesh().num_elements());
    for (typename MeshType::ElementIndex e(0); e < this->mesh().num_elements();
         ++e) {
      gradients_.push_back(CalcGradientVector(e));
    }
  }

  Vector3<T> CalcGradientVector(typename MeshType::ElementIndex e) const {
    std::array<T, MeshType::kVertexPerElement> u;
    for (int i = 0; i < MeshType::kVertexPerElement; ++i) {
      u[i] = values_[this->mesh().element(e).vertex(i)];
    }
    return this->mesh().CalcGradientVectorOfLinearField(u, e);
  }

  void CalcValueAtMeshOriginForAllElements() {
    values_at_Mo_.clear();
    values_at_Mo_.reserve(this->mesh().num_elements());
    for (typename MeshType::ElementIndex e(0); e < this->mesh().num_elements();
         ++e) {
      values_at_Mo_.push_back(CalcValueAtMeshOrigin(e));
    }
  }

  T CalcValueAtMeshOrigin(typename MeshType::ElementIndex e) const {
    DRAKE_DEMAND(e < gradients_.size());
    const typename MeshType::VertexIndex v0 = this->mesh().element(e).vertex(0);
    const Vector3<T>& p_MV0 = this->mesh().vertex(v0);
    // f(V₀) = ∇fᵉ⋅p_MV₀ + fᵉ(Mo)
    // fᵉ(Mo) = f(V₀) - ∇fᵉ⋅p_MV₀
    return values_[v0] - gradients_[e].dot(p_MV0);
  }

  // We use `reset_on_copy` so that the default copy constructor resets
  // the pointer to null when a MeshFieldLinear is copied.
  reset_on_copy<const MeshType*> mesh_;

  // (Deprecated.)
  std::string name_;
  // The field values are indexed in the same way as vertices, i.e.,
  // values_[i] is the field value for the mesh vertices_[i].
  std::vector<T> values_;
  // The gradients are indexed in the same way as elements, i.e.,
  // gradients_[i] is the gradient vector on elements_[i]. The elements could
  // be tetrahedra for VolumeMesh or triangles for SurfaceMesh.
  std::vector<Vector3<T>> gradients_;
  // values_at_Mo_[i] is the value of the linear function that represents the
  // piecewise linear field on the mesh elements_[i] at Mo the origin of
  // frame M of the mesh. Notice that Mo may or may not lie inside elements_[i].
  std::vector<T> values_at_Mo_;
};

}  // namespace geometry
}  // namespace drake

