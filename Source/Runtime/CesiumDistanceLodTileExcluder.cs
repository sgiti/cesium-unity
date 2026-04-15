using System;
using UnityEngine;

namespace CesiumForUnity
{
    /// <summary>
    /// Excludes tiles whose level is more detailed than a configured level for the current camera distance.
    /// </summary>
    public class CesiumDistanceLodTileExcluder : CesiumTileExcluder
    {
        [Serializable]
        public struct DistanceLodLevel
        {
            [Tooltip("Distance from camera to the closest point of a tile's bounds, in Unity meters.")]
            public double cameraDistance;

            [Tooltip("Maximum tile level that can be loaded and rendered at this distance.")]
            public int lodLevel;
        }

        [SerializeField]
        [Tooltip("Distance-to-LOD mapping entries. Keep entries sorted by ascending cameraDistance.")]
        private DistanceLodLevel[] _distanceLodLevels = Array.Empty<DistanceLodLevel>();

        [SerializeField]
        [Tooltip("If set, this camera is used to evaluate distance. Otherwise Camera.main is used.")]
        private Camera _targetCamera;

        public DistanceLodLevel[] distanceLodLevels
        {
            get => this._distanceLodLevels;
            set => this._distanceLodLevels = value ?? Array.Empty<DistanceLodLevel>();
        }

        public Camera targetCamera
        {
            get => this._targetCamera;
            set => this._targetCamera = value;
        }

        public override bool ShouldExclude(Cesium3DTile tile)
        {
            if (this._distanceLodLevels == null || this._distanceLodLevels.Length == 0)
                return false;

            Camera camera = this._targetCamera != null ? this._targetCamera : Camera.main;
            if (camera == null)
                return false;

            Vector3 cameraPosition = camera.transform.position;
            float tileDistance = Vector3.Distance(cameraPosition, tile.bounds.ClosestPoint(cameraPosition));
            int targetLevel = this.GetTargetLevel(tileDistance);

            // Only allow up to the selected level so deeper descendants are never loaded.
            return tile.level > targetLevel;
        }

        private int GetTargetLevel(float cameraDistance)
        {
            DistanceLodLevel[] levels = this._distanceLodLevels;
            int targetLevel = levels[levels.Length - 1].lodLevel;

            for (int i = 0; i < levels.Length; i++)
            {
                if (cameraDistance <= levels[i].cameraDistance)
                    return levels[i].lodLevel;
            }

            return targetLevel;
        }
    }
}
