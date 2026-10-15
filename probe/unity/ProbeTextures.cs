using System;
using System.IO;
using UnityEngine;

namespace FrameAppPayloadProbe
{
    public sealed class ProbeTexture : IDisposable
    {
        public readonly ProbeManifest Manifest;
        public readonly Texture2D Texture;
        public readonly string PngSha256;

        private ProbeTexture(ProbeManifest manifest, Texture2D texture, string pngHash)
        {
            Manifest = manifest;
            Texture = texture;
            PngSha256 = pngHash;
        }

        public static ProbeTexture Load(string directory, string eye)
        {
            string root = Path.GetFullPath(directory);
            if ((File.GetAttributes(root) & FileAttributes.ReparsePoint) != 0)
                throw new InvalidDataException("Payload directory must not be a link.");
            var manifest = ProbeManifest.Load(Path.Combine(root, eye + "-source.json"), eye);
            byte[] png = ProbeManifest.ReadRegular(Path.Combine(root, eye + ".png"), ProbeManifest.MaximumPngBytes);
            CheckPng(png, manifest.Placements.Canvas[0], manifest.Placements.Canvas[1]);
            var texture = new Texture2D(2, 2, TextureFormat.RGB24, false, false);
            try
            {
                if (!ImageConversion.LoadImage(texture, png, false)
                    || texture.width != manifest.Placements.Canvas[0] || texture.height != manifest.Placements.Canvas[1])
                    throw new InvalidDataException("Unity did not decode the exact declared canvas extent.");
                Color32[] pixels = texture.GetPixels32();
                byte[] rgb = TopDownRgb(pixels, texture.width, texture.height);
                if (ProbeManifest.Sha256(rgb) != manifest.CanvasSha256)
                    throw new InvalidDataException("Unity RGB decoding/orientation differs from the trusted source canvas.");
                CheckHeaders(rgb, texture.width, manifest);
                CheckPlanes(rgb, texture.width, manifest);
                texture.name = "FrameAppPayloadProbe." + eye;
                texture.filterMode = FilterMode.Point;
                texture.wrapMode = TextureWrapMode.Clamp;
                texture.Apply(false, true);
                return new ProbeTexture(manifest, texture, ProbeManifest.Sha256(png));
            }
            catch
            {
                UnityEngine.Object.Destroy(texture);
                throw;
            }
        }

        public void Dispose()
        {
            if (Texture != null)
                UnityEngine.Object.Destroy(Texture);
        }

        // Unity GetPixels32 begins at the lower-left. The source PNG/hash begins top-left.
        public static byte[] TopDownRgb(Color32[] pixels, int width, int height)
        {
            if (pixels.Length != width * height)
                throw new InvalidDataException("Unexpected Unity pixel count.");
            byte[] rgb = new byte[pixels.Length * 3];
            for (int y = 0; y < height; y++)
                for (int x = 0; x < width; x++)
                {
                    Color32 pixel = pixels[(height - 1 - y) * width + x];
                    int offset = (y * width + x) * 3;
                    rgb[offset] = pixel.r;
                    rgb[offset + 1] = pixel.g;
                    rgb[offset + 2] = pixel.b;
                }
            return rgb;
        }

        private static void CheckHeaders(byte[] rgb, int width, ProbeManifest manifest)
        {
            byte[] wanted = ExpectedHeader(manifest);
            for (int copy = 0; copy < 3; copy++)
                for (int bit = 0; bit < ProbeManifest.HeaderBytes * 8; bit++)
                {
                    int expected = (wanted[bit / 8] >> (7 - bit % 8)) & 1;
                    int x = 16 + bit % 64 * 8;
                    int y = 16 + copy * 76 + bit / 64 * 4;
                    for (int side = 0; side < 2; side++)
                        for (int dy = 0; dy < 4; dy++)
                            for (int dx = 0; dx < 4; dx++)
                            {
                                int offset = ((y + dy) * width + x + side * 4 + dx) * 3;
                                byte value = (byte)(255 * (side == 0 ? expected : 1 - expected));
                                if (rgb[offset] != value || rgb[offset + 1] != value || rgb[offset + 2] != value)
                                    throw new InvalidDataException("Source header does not encode the trusted identity and planes.");
                            }
                }
        }

        private static byte[] ExpectedHeader(ProbeManifest manifest)
        {
            byte[] bytes = new byte[ProbeManifest.HeaderBytes];
            byte[] magic = System.Text.Encoding.ASCII.GetBytes("FAPPROB1");
            Array.Copy(magic, bytes, magic.Length);
            bytes[9] = 1;
            bytes[11] = 1;
            Array.Copy(Unhex(manifest.Session), 0, bytes, 12, 16);
            for (int index = 0; index < 8; index++)
                bytes[28 + index] = (byte)(manifest.ContentId >> (56 - index * 8));
            bytes[36] = (byte)(manifest.Eye == "right" ? 1 : 0);
            int[] coordinates = new int[12];
            Array.Copy(manifest.SourceExtent, 0, coordinates, 0, 2);
            Array.Copy(manifest.Placements.Canvas, 0, coordinates, 2, 2);
            Array.Copy(manifest.Placements.Color, 0, coordinates, 4, 4);
            Array.Copy(manifest.Placements.Coverage, 0, coordinates, 8, 4);
            for (int index = 0; index < coordinates.Length; index++)
            {
                bytes[40 + index * 2] = (byte)(coordinates[index] >> 8);
                bytes[41 + index * 2] = (byte)coordinates[index];
            }
            Array.Copy(Unhex(manifest.ColorSha256), 0, bytes, 64, 32);
            Array.Copy(Unhex(manifest.CoverageSha256), 0, bytes, 96, 32);
            uint crc = Crc32(bytes, 0, 128);
            for (int index = 0; index < 4; index++)
                bytes[128 + index] = (byte)(crc >> (24 - index * 8));
            return bytes;
        }

        private static void CheckPlanes(byte[] rgb, int canvasWidth, ProbeManifest manifest)
        {
            int width = manifest.SourceExtent[0], height = manifest.SourceExtent[1];
            byte[] color = new byte[width * height * 3];
            byte[] coverage = new byte[width * height];
            for (int y = 0; y < height; y++)
            {
                int colorOffset = ((manifest.Placements.Color[1] + y) * canvasWidth + manifest.Placements.Color[0]) * 3;
                Array.Copy(rgb, colorOffset, color, y * width * 3, width * 3);
                for (int x = 0; x < width; x++)
                {
                    int offset = ((manifest.Placements.Coverage[1] + y) * canvasWidth + manifest.Placements.Coverage[0] + x) * 3;
                    if (rgb[offset] != rgb[offset + 1] || rgb[offset] != rgb[offset + 2])
                        throw new InvalidDataException("Source coverage is not R8 grayscale.");
                    coverage[y * width + x] = rgb[offset];
                }
            }
            if (ProbeManifest.Sha256(color) != manifest.ColorSha256 || ProbeManifest.Sha256(coverage) != manifest.CoverageSha256)
                throw new InvalidDataException("Source RGB/R8 digest mismatch.");
        }

        private static byte[] Unhex(string value)
        {
            byte[] bytes = new byte[value.Length / 2];
            for (int index = 0; index < bytes.Length; index++)
                bytes[index] = Convert.ToByte(value.Substring(index * 2, 2), 16);
            return bytes;
        }

        private static uint Read32(byte[] data, int offset)
        {
            return ((uint)data[offset] << 24) | ((uint)data[offset + 1] << 16)
                   | ((uint)data[offset + 2] << 8) | data[offset + 3];
        }

        private static uint Crc32(byte[] data, int start, int count)
        {
            uint result = 0xffffffff;
            for (int index = start; index < start + count; index++)
            {
                result ^= data[index];
                for (int bit = 0; bit < 8; bit++)
                    result = (result >> 1) ^ ((result & 1) == 0 ? 0u : 0xedb88320u);
            }
            return ~result;
        }

        private static void CheckPng(byte[] bytes, int width, int height)
        {
            byte[] signature = { 137, 80, 78, 71, 13, 10, 26, 10 };
            if (bytes.Length < 45)
                throw new InvalidDataException("PNG is truncated.");
            for (int index = 0; index < signature.Length; index++)
                if (bytes[index] != signature[index])
                    throw new InvalidDataException("PNG signature mismatch.");
            int offset = 8, chunks = 0;
            bool idat = false, idatClosed = false;
            while (offset + 12 <= bytes.Length && chunks++ < 4096)
            {
                uint length = Read32(bytes, offset);
                if (length > (uint)(bytes.Length - offset - 12))
                    throw new InvalidDataException("PNG chunk exceeds its file.");
                int size = (int)length;
                string kind = System.Text.Encoding.ASCII.GetString(bytes, offset + 4, 4);
                if (Crc32(bytes, offset + 4, size + 4) != Read32(bytes, offset + size + 8))
                    throw new InvalidDataException("PNG CRC mismatch.");
                if (chunks == 1)
                {
                    if (kind != "IHDR" || size != 13 || Read32(bytes, offset + 8) != width
                        || Read32(bytes, offset + 12) != height || bytes[offset + 16] != 8
                        || bytes[offset + 17] != 2 || bytes[offset + 18] != 0
                        || bytes[offset + 19] != 0 || bytes[offset + 20] != 0)
                        throw new InvalidDataException("PNG must match the bounded RGB8 source canvas.");
                }
                else if (kind == "IDAT")
                {
                    if (idatClosed)
                        throw new InvalidDataException("PNG IDAT sequence is not contiguous.");
                    idat = true;
                }
                else if (kind == "IEND")
                {
                    if (!idat || size != 0 || offset + 12 != bytes.Length)
                        throw new InvalidDataException("PNG ending is invalid.");
                    return;
                }
                else if (kind != "tEXt" || size > 4096)
                    throw new InvalidDataException("Unsupported PNG chunk or conversion.");
                else if (idat)
                    idatClosed = true;
                offset += size + 12;
            }
            throw new InvalidDataException("PNG is incomplete or exceeds the chunk bound.");
        }
    }
}
