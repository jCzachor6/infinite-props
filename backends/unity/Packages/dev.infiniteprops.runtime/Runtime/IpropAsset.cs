using System;
using UnityEngine;

namespace InfiniteProps
{
    public sealed class IpropAsset : ScriptableObject
    {
        [SerializeField, HideInInspector] private byte[] bytes;
        public void SetBytes(byte[] value) { bytes = value == null ? throw new ArgumentNullException(nameof(value)) : (byte[])value.Clone(); }
        public Program Load() => new Program(bytes);
    }
}
