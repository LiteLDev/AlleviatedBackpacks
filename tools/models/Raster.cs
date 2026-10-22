// Tiny software rasterizer for the backpack GUI icons (tools/models/backpack.ps1).
// Renders textured quads with a z-buffer into per-layer RGBA buffers.
using System;
using System.Collections.Generic;

namespace AbpModels {

public sealed class Quad {
    public double[] X = new double[4], Y = new double[4], Z = new double[4]; // model space (block px)
    public double[] U = new double[4], V = new double[4];                    // atlas texels
    public int Layer;      // output layer
    public int Sampler;    // texture index
    public bool Occluder = true; // writes depth (glass does not)
}

public sealed class Raster {
    public readonly int N;
    public readonly double[] Depth;
    public readonly int[] Owner;     // layer index of the visible pixel, -1 none
    public readonly uint[] Color;    // shaded ARGB of the visible pixel
    public readonly uint[] Over;     // non-occluding overlay (glass) in front of the scene
    readonly List<uint[]> textures = new List<uint[]>();
    readonly List<int> texW = new List<int>();
    readonly List<int> texH = new List<int>();
    // Model transform (SB "gui" display transform) -> view space.
    public double[,] M = new double[3, 3];
    public double Tx, Ty, Tz, S = 1;
    public double[] L0 = Norm(-0.55, 1.0, 0.65), L1 = Norm(0.35, 0.25, 0.9);
    public double Ambient = 0.42, Diffuse = 0.6;

    public Raster(int n) {
        N = n;
        Depth = new double[n * n];
        Owner = new int[n * n];
        Color = new uint[n * n];
        Over = new uint[n * n];
        Clear();
    }

    public void Clear() {
        for (int i = 0; i < Depth.Length; i++) { Depth[i] = double.NegativeInfinity; Owner[i] = -1; Color[i] = 0; Over[i] = 0; }
    }

    public int AddTexture(uint[] argb, int w, int h) { textures.Add(argb); texW.Add(w); texH.Add(h); return textures.Count - 1; }

    static double[] Norm(double x, double y, double z) { double l = Math.Sqrt(x * x + y * y + z * z); return new[] { x / l, y / l, z / l }; }

    /// Java JOML rotationXYZ(rx, ry, rz) (degrees): v' = Rx * Ry * Rz * v.
    public void SetRotation(double rx, double ry, double rz) {
        double a = rx * Math.PI / 180, b = ry * Math.PI / 180, c = rz * Math.PI / 180;
        var Rx = new double[,] { { 1, 0, 0 }, { 0, Math.Cos(a), -Math.Sin(a) }, { 0, Math.Sin(a), Math.Cos(a) } };
        var Ry = new double[,] { { Math.Cos(b), 0, Math.Sin(b) }, { 0, 1, 0 }, { -Math.Sin(b), 0, Math.Cos(b) } };
        var Rz = new double[,] { { Math.Cos(c), -Math.Sin(c), 0 }, { Math.Sin(c), Math.Cos(c), 0 }, { 0, 0, 1 } };
        M = Mul(Mul(Rx, Ry), Rz);
    }

    static double[,] Mul(double[,] a, double[,] b) {
        var r = new double[3, 3];
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { double s = 0; for (int k = 0; k < 3; k++) s += a[i, k] * b[k, j]; r[i, j] = s; }
        return r;
    }

    void Xf(double x, double y, double z, out double ox, out double oy, out double oz) {
        // block px -> centered block units, rotate, scale, translate (block units)
        double px = x / 16.0 - 0.5, py = y / 16.0 - 0.5, pz = z / 16.0 - 0.5;
        ox = (M[0, 0] * px + M[0, 1] * py + M[0, 2] * pz) * S + Tx;
        oy = (M[1, 0] * px + M[1, 1] * py + M[1, 2] * pz) * S + Ty;
        oz = (M[2, 0] * px + M[2, 1] * py + M[2, 2] * pz) * S + Tz;
    }

    public void Draw(Quad q) {
        var sx = new double[4]; var sy = new double[4]; var sz = new double[4];
        var vx = new double[4]; var vy = new double[4]; var vz = new double[4];
        for (int i = 0; i < 4; i++) {
            Xf(q.X[i], q.Y[i], q.Z[i], out vx[i], out vy[i], out vz[i]);
            // one block unit = N pixels (a GUI slot is 16 GUI px = 1 block)
            sx[i] = N / 2.0 + vx[i] * N;
            sy[i] = N / 2.0 - vy[i] * N;
            sz[i] = vz[i];
        }
        // normal (view space)
        double ax = vx[1] - vx[0], ay = vy[1] - vy[0], az = vz[1] - vz[0];
        double bx = vx[3] - vx[0], by = vy[3] - vy[0], bz = vz[3] - vz[0];
        double nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
        double nl = Math.Sqrt(nx * nx + ny * ny + nz * nz);
        if (nl < 1e-12) return;
        nx /= -nl; ny /= -nl; nz /= -nl;
        if (nz <= 1e-9) return; // back face
        double light = Ambient + Diffuse * (Math.Max(0, nx * L0[0] + ny * L0[1] + nz * L0[2]) + Math.Max(0, nx * L1[0] + ny * L1[1] + nz * L1[2]));
        if (light > 1) light = 1;
        Tri(q, sx, sy, sz, 0, 1, 2, light);
        Tri(q, sx, sy, sz, 0, 2, 3, light);
    }

    void Tri(Quad q, double[] sx, double[] sy, double[] sz, int i0, int i1, int i2, double light) {
        int minX = Math.Max(0, (int)Math.Floor(Math.Min(sx[i0], Math.Min(sx[i1], sx[i2]))));
        int maxX = Math.Min(N - 1, (int)Math.Ceiling(Math.Max(sx[i0], Math.Max(sx[i1], sx[i2]))));
        int minY = Math.Max(0, (int)Math.Floor(Math.Min(sy[i0], Math.Min(sy[i1], sy[i2]))));
        int maxY = Math.Min(N - 1, (int)Math.Ceiling(Math.Max(sy[i0], Math.Max(sy[i1], sy[i2]))));
        double den = (sy[i1] - sy[i2]) * (sx[i0] - sx[i2]) + (sx[i2] - sx[i1]) * (sy[i0] - sy[i2]);
        if (Math.Abs(den) < 1e-12) return;
        var tex = textures[q.Sampler]; int tw = texW[q.Sampler], th = texH[q.Sampler];
        for (int y = minY; y <= maxY; y++) {
            for (int x = minX; x <= maxX; x++) {
                double px = x + 0.5, py = y + 0.5;
                double w0 = ((sy[i1] - sy[i2]) * (px - sx[i2]) + (sx[i2] - sx[i1]) * (py - sy[i2])) / den;
                double w1 = ((sy[i2] - sy[i0]) * (px - sx[i2]) + (sx[i0] - sx[i2]) * (py - sy[i2])) / den;
                double w2 = 1 - w0 - w1;
                if (w0 < -1e-9 || w1 < -1e-9 || w2 < -1e-9) continue;
                double z = w0 * sz[i0] + w1 * sz[i1] + w2 * sz[i2];
                int idx = y * N + x;
                if (z <= Depth[idx] + 1e-9) continue;
                double u = w0 * q.U[i0] + w1 * q.U[i1] + w2 * q.U[i2];
                double v = w0 * q.V[i0] + w1 * q.V[i1] + w2 * q.V[i2];
                int tu = Math.Min(tw - 1, Math.Max(0, (int)Math.Floor(u)));
                int tv = Math.Min(th - 1, Math.Max(0, (int)Math.Floor(v)));
                uint c = tex[tv * tw + tu];
                uint a = c >> 24;
                if (a == 0) continue;
                uint sr = (uint)Math.Min(255, Math.Round(((c >> 16) & 255) * light));
                uint sg = (uint)Math.Min(255, Math.Round(((c >> 8) & 255) * light));
                uint sb = (uint)Math.Min(255, Math.Round((c & 255) * light));
                if (!q.Occluder) { Over[idx] = (a << 24) | (sr << 16) | (sg << 8) | sb; continue; }
                {
                    Depth[idx] = z;
                    Owner[idx] = q.Layer;
                    Color[idx] = (a << 24) | (sr << 16) | (sg << 8) | sb;
                }
            }
        }
    }
}
}
