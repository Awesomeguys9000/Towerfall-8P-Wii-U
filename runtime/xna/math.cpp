// Out-of-line members of the XNA math types (Vector2/3/4, Point, Rectangle, Color, Matrix, MathHelper).
// Behaviour follows XNA 4.0 / FNA exactly (row-vector convention: v' = v * M).
#include "xna/xna.h"
#include <string>
#include <cmath>
#include <algorithm>

namespace Microsoft { namespace Xna { namespace Framework {

namespace {
std::string fs(float v) { return ::sys::ToString(v)->ToUtf8(); }
std::string is(int32_t v) { return std::to_string(v); }
String* mk(const std::string& s) { return String::FromUtf8(s.c_str(), s.size()); }
}

// ------------------------------------------------------------------ ToString (XNA formats)
String* Vector2::ToString() { return mk("{X:" + fs(X) + " Y:" + fs(Y) + "}"); }
String* Vector3::ToString() { return mk("{X:" + fs(X) + " Y:" + fs(Y) + " Z:" + fs(Z) + "}"); }
String* Vector4::ToString() { return mk("{X:" + fs(X) + " Y:" + fs(Y) + " Z:" + fs(Z) + " W:" + fs(W) + "}"); }
String* Point::ToString() { return mk("{X:" + is(X) + " Y:" + is(Y) + "}"); }
String* Rectangle::ToString() { return mk("{X:" + is(X) + " Y:" + is(Y) + " Width:" + is(Width) + " Height:" + is(Height) + "}"); }
String* Color::ToString() { return mk("{R:" + is(r) + " G:" + is(g) + " B:" + is(b) + " A:" + is(a) + "}"); }
String* Matrix::ToString()
{
    return mk("{ {M11:" + fs(M11) + " M12:" + fs(M12) + " M13:" + fs(M13) + " M14:" + fs(M14) + "} "
              "{M21:" + fs(M21) + " M22:" + fs(M22) + " M23:" + fs(M23) + " M24:" + fs(M24) + "} "
              "{M31:" + fs(M31) + " M32:" + fs(M32) + " M33:" + fs(M33) + " M34:" + fs(M34) + "} "
              "{M41:" + fs(M41) + " M42:" + fs(M42) + " M43:" + fs(M43) + " M44:" + fs(M44) + "} }");
}

// ------------------------------------------------------------------ Vector2
Vector2 Vector2::Transform(Vector2 v, const Matrix& m)
{
    return Vector2(v.X * m.M11 + v.Y * m.M21 + m.M41, v.X * m.M12 + v.Y * m.M22 + m.M42);
}

// ------------------------------------------------------------------ Rectangle
Rectangle Rectangle::Intersect(Rectangle a, Rectangle b)
{
    if (!a.Intersects(b)) return Rectangle(0, 0, 0, 0);
    int32_t right = std::min(a.X + a.Width, b.X + b.Width);
    int32_t left = std::max(a.X, b.X);
    int32_t top = std::max(a.Y, b.Y);
    int32_t bottom = std::min(a.Y + a.Height, b.Y + b.Height);
    return Rectangle(left, top, right - left, bottom - top);
}

Rectangle Rectangle::Union(Rectangle a, Rectangle b)
{
    int32_t x = std::min(a.X, b.X);
    int32_t y = std::min(a.Y, b.Y);
    return Rectangle(x, y, std::max(a.get_Right(), b.get_Right()) - x, std::max(a.get_Bottom(), b.get_Bottom()) - y);
}

// ------------------------------------------------------------------ Color
Color Color::Lerp(Color a, Color b, float t)
{
    t = MathHelper::Clamp(t, 0.0f, 1.0f);
    return Color((int32_t)MathHelper::Lerp(a.r, b.r, t), (int32_t)MathHelper::Lerp(a.g, b.g, t),
                 (int32_t)MathHelper::Lerp(a.b, b.b, t), (int32_t)MathHelper::Lerp(a.a, b.a, t));
}

// ------------------------------------------------------------------ MathHelper
float MathHelper::WrapAngle(float angle)
{
    angle = (float)std::remainder((double)angle, 6.2831854820251465);
    if (angle <= -3.14159274f) angle += 6.28318548f;
    else if (angle > 3.14159274f) angle -= 6.28318548f;
    return angle;
}

// ------------------------------------------------------------------ Matrix
Matrix operator*(const Matrix& a, const Matrix& b)
{
    Matrix r;
    r.M11 = a.M11 * b.M11 + a.M12 * b.M21 + a.M13 * b.M31 + a.M14 * b.M41;
    r.M12 = a.M11 * b.M12 + a.M12 * b.M22 + a.M13 * b.M32 + a.M14 * b.M42;
    r.M13 = a.M11 * b.M13 + a.M12 * b.M23 + a.M13 * b.M33 + a.M14 * b.M43;
    r.M14 = a.M11 * b.M14 + a.M12 * b.M24 + a.M13 * b.M34 + a.M14 * b.M44;
    r.M21 = a.M21 * b.M11 + a.M22 * b.M21 + a.M23 * b.M31 + a.M24 * b.M41;
    r.M22 = a.M21 * b.M12 + a.M22 * b.M22 + a.M23 * b.M32 + a.M24 * b.M42;
    r.M23 = a.M21 * b.M13 + a.M22 * b.M23 + a.M23 * b.M33 + a.M24 * b.M43;
    r.M24 = a.M21 * b.M14 + a.M22 * b.M24 + a.M23 * b.M34 + a.M24 * b.M44;
    r.M31 = a.M31 * b.M11 + a.M32 * b.M21 + a.M33 * b.M31 + a.M34 * b.M41;
    r.M32 = a.M31 * b.M12 + a.M32 * b.M22 + a.M33 * b.M32 + a.M34 * b.M42;
    r.M33 = a.M31 * b.M13 + a.M32 * b.M23 + a.M33 * b.M33 + a.M34 * b.M43;
    r.M34 = a.M31 * b.M14 + a.M32 * b.M24 + a.M33 * b.M34 + a.M34 * b.M44;
    r.M41 = a.M41 * b.M11 + a.M42 * b.M21 + a.M43 * b.M31 + a.M44 * b.M41;
    r.M42 = a.M41 * b.M12 + a.M42 * b.M22 + a.M43 * b.M32 + a.M44 * b.M42;
    r.M43 = a.M41 * b.M13 + a.M42 * b.M23 + a.M43 * b.M33 + a.M44 * b.M43;
    r.M44 = a.M41 * b.M14 + a.M42 * b.M24 + a.M43 * b.M34 + a.M44 * b.M44;
    return r;
}

Matrix Matrix::Lerp(Matrix a, Matrix b, float t)
{
    Matrix r;
    const float* pa = &a.M11; const float* pb = &b.M11; float* pr = &r.M11;
    for (int i = 0; i < 16; i++) pr[i] = pa[i] + (pb[i] - pa[i]) * t;
    return r;
}

Matrix Matrix::Invert(Matrix m)
{
    // XNA 4.0 Matrix.Invert (cofactor expansion, same operation order)
    float n1 = m.M11, n2 = m.M12, n3 = m.M13, n4 = m.M14;
    float n5 = m.M21, n6 = m.M22, n7 = m.M23, n8 = m.M24;
    float n9 = m.M31, n10 = m.M32, n11 = m.M33, n12 = m.M34;
    float n13 = m.M41, n14 = m.M42, n15 = m.M43, n16 = m.M44;
    float n17 = n11 * n16 - n12 * n15;
    float n18 = n10 * n16 - n12 * n14;
    float n19 = n10 * n15 - n11 * n14;
    float n20 = n9 * n16 - n12 * n13;
    float n21 = n9 * n15 - n11 * n13;
    float n22 = n9 * n14 - n10 * n13;
    float n23 = n6 * n17 - n7 * n18 + n8 * n19;
    float n24 = -(n5 * n17 - n7 * n20 + n8 * n21);
    float n25 = n5 * n18 - n6 * n20 + n8 * n22;
    float n26 = -(n5 * n19 - n6 * n21 + n7 * n22);
    float n27 = 1.0f / (n1 * n23 + n2 * n24 + n3 * n25 + n4 * n26);
    Matrix r;
    r.M11 = n23 * n27;
    r.M21 = n24 * n27;
    r.M31 = n25 * n27;
    r.M41 = n26 * n27;
    r.M12 = -(n2 * n17 - n3 * n18 + n4 * n19) * n27;
    r.M22 = (n1 * n17 - n3 * n20 + n4 * n21) * n27;
    r.M32 = -(n1 * n18 - n2 * n20 + n4 * n22) * n27;
    r.M42 = (n1 * n19 - n2 * n21 + n3 * n22) * n27;
    float n28 = n7 * n16 - n8 * n15;
    float n29 = n6 * n16 - n8 * n14;
    float n30 = n6 * n15 - n7 * n14;
    float n31 = n5 * n16 - n8 * n13;
    float n32 = n5 * n15 - n7 * n13;
    float n33 = n5 * n14 - n6 * n13;
    r.M13 = (n2 * n28 - n3 * n29 + n4 * n30) * n27;
    r.M23 = -(n1 * n28 - n3 * n31 + n4 * n32) * n27;
    r.M33 = (n1 * n29 - n2 * n31 + n4 * n33) * n27;
    r.M43 = -(n1 * n30 - n2 * n32 + n3 * n33) * n27;
    float n34 = n7 * n12 - n8 * n11;
    float n35 = n6 * n12 - n8 * n10;
    float n36 = n6 * n11 - n7 * n10;
    float n37 = n5 * n12 - n8 * n9;
    float n38 = n5 * n11 - n7 * n9;
    float n39 = n5 * n10 - n6 * n9;
    r.M14 = -(n2 * n34 - n3 * n35 + n4 * n36) * n27;
    r.M24 = (n1 * n34 - n3 * n37 + n4 * n38) * n27;
    r.M34 = -(n1 * n35 - n2 * n37 + n4 * n39) * n27;
    r.M44 = (n1 * n36 - n2 * n38 + n3 * n39) * n27;
    return r;
}

}}} // namespace Microsoft::Xna::Framework
