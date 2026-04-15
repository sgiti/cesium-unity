#include "Cesium3DTileImpl.h"

#include "UnityTransforms.h"

#include <Cesium3DTilesSelection/Tile.h>
#include <CesiumGeospatial/Ellipsoid.h>

#include <DotNet/Unity/Mathematics/double4x4.h>
#include <DotNet/UnityEngine/Bounds.h>

#include <type_traits>

using namespace Cesium3DTilesSelection;
using namespace CesiumGeometry;
using namespace CesiumGeospatial;

namespace CesiumForUnityNative {

namespace {

template <typename T, typename = void>
struct HasGetDepth : std::false_type {};

template <typename T>
struct HasGetDepth<
    T,
    std::void_t<decltype(std::declval<const T&>().getDepth())>>
    : std::true_type {};

template <typename T, typename = void>
struct HasGetParent : std::false_type {};

template <typename T>
struct HasGetParent<
    T,
    std::void_t<decltype(std::declval<const T&>().getParent())>>
    : std::true_type {};

} // namespace

DotNet::UnityEngine::Bounds Cesium3DTileImpl::getBounds(
    void* pTileVoid,
    void* pTileEllipsoidVoid,
    const DotNet::Unity::Mathematics::double4x4& ecefToLocalMatrix) {
  const Tile* pTile = static_cast<const Tile*>(pTileVoid);
  const Ellipsoid* pTileEllipsoid =
      static_cast<const Ellipsoid*>(pTileEllipsoidVoid);
  const BoundingVolume& bv = pTile->getBoundingVolume();
  OrientedBoundingBox obb =
      getOrientedBoundingBoxFromBoundingVolume(bv, *pTileEllipsoid);
  obb = obb.transform(UnityTransforms::fromUnity(ecefToLocalMatrix));
  AxisAlignedBox aabb = obb.toAxisAligned();
  return DotNet::UnityEngine::Bounds::Construct(
      UnityTransforms::toUnity(aabb.center),
      DotNet::UnityEngine::Vector3{
          float(aabb.lengthX),
          float(aabb.lengthY),
          float(aabb.lengthZ)});
}

int32_t Cesium3DTileImpl::getLevel(void* pTileVoid) {
  const Tile* pTile = static_cast<const Tile*>(pTileVoid);

  if constexpr (HasGetDepth<Tile>::value) {
    return static_cast<int32_t>(pTile->getDepth());
  }

  if constexpr (HasGetParent<Tile>::value) {
    int32_t depth = 0;
    const Tile* pCurrent = pTile;
    while (pCurrent != nullptr) {
      const auto* pParent = pCurrent->getParent();
      if (pParent == nullptr) {
        break;
      }

      ++depth;
      pCurrent = pParent;
    }

    return depth;
  }

  return -1;
}

} // namespace CesiumForUnityNative
