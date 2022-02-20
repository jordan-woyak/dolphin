// Copyright 2019 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Common/Matrix.h"

#include <algorithm>
#include <cmath>

#include "common/xr_linear.h"

namespace
{
// Multiply a NxM matrix by a NxP matrix.
template <int N, int M, int P, typename T>
auto MatrixMultiply(const std::array<T, N * M>& a, const std::array<T, M * P>& b)
    -> std::array<T, N * P>
{
  std::array<T, N * P> result;

  for (int n = 0; n != N; ++n)
  {
    for (int p = 0; p != P; ++p)
    {
      T temp = {};
      for (int m = 0; m != M; ++m)
      {
        temp += a[n * M + m] * b[m * P + p];
      }
      result[n * P + p] = temp;
    }
  }

  return result;
}

}  // namespace

namespace Common
{
Matrix33 Matrix33::Identity()
{
  Matrix33 mtx = {};
  mtx.data[0] = 1.0f;
  mtx.data[4] = 1.0f;
  mtx.data[8] = 1.0f;
  return mtx;
}

Matrix33 Matrix33::FromQuaternion(float qx, float qy, float qz, float qw)
{
  // Normalize.
  const float n = 1 / std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
  qx *= n;
  qy *= n;
  qz *= n;
  qw *= n;

  return {
      1 - 2 * qy * qy - 2 * qz * qz, 2 * qx * qy - 2 * qz * qw,     2 * qx * qz + 2 * qy * qw,
      2 * qx * qy + 2 * qz * qw,     1 - 2 * qx * qx - 2 * qz * qz, 2 * qy * qz - 2 * qx * qw,
      2 * qx * qz - 2 * qy * qw,     2 * qy * qz + 2 * qx * qw,     1 - 2 * qx * qx - 2 * qy * qy,
  };
}

Matrix33 Matrix33::RotateX(float rad)
{
  const float s = std::sin(rad);
  const float c = std::cos(rad);
  Matrix33 mtx = {};
  mtx.data[0] = 1;
  mtx.data[4] = c;
  mtx.data[5] = -s;
  mtx.data[7] = s;
  mtx.data[8] = c;
  return mtx;
}

Matrix33 Matrix33::RotateY(float rad)
{
  const float s = std::sin(rad);
  const float c = std::cos(rad);
  Matrix33 mtx = {};
  mtx.data[0] = c;
  mtx.data[2] = s;
  mtx.data[4] = 1;
  mtx.data[6] = -s;
  mtx.data[8] = c;
  return mtx;
}

Matrix33 Matrix33::RotateZ(float rad)
{
  const float s = std::sin(rad);
  const float c = std::cos(rad);
  Matrix33 mtx = {};
  mtx.data[0] = c;
  mtx.data[1] = -s;
  mtx.data[3] = s;
  mtx.data[4] = c;
  mtx.data[8] = 1;
  return mtx;
}

Matrix33 Matrix33::Rotate(float rad, const Vec3& axis)
{
  const float s = std::sin(rad);
  const float c = std::cos(rad);
  Matrix33 mtx;
  mtx.data[0] = axis.x * axis.x * (1 - c) + c;
  mtx.data[1] = axis.x * axis.y * (1 - c) - axis.z * s;
  mtx.data[2] = axis.x * axis.z * (1 - c) + axis.y * s;
  mtx.data[3] = axis.y * axis.x * (1 - c) + axis.z * s;
  mtx.data[4] = axis.y * axis.y * (1 - c) + c;
  mtx.data[5] = axis.y * axis.z * (1 - c) - axis.x * s;
  mtx.data[6] = axis.z * axis.x * (1 - c) - axis.y * s;
  mtx.data[7] = axis.z * axis.y * (1 - c) + axis.x * s;
  mtx.data[8] = axis.z * axis.z * (1 - c) + c;
  return mtx;
}

Matrix33 Matrix33::Scale(const Vec3& vec)
{
  Matrix33 mtx = {};
  mtx.data[0] = vec.x;
  mtx.data[4] = vec.y;
  mtx.data[8] = vec.z;
  return mtx;
}

void Matrix33::Multiply(const Matrix33& a, const Matrix33& b, Matrix33* result)
{
  result->data = MatrixMultiply<3, 3, 3>(a.data, b.data);
}

void Matrix33::Multiply(const Matrix33& a, const Vec3& vec, Vec3* result)
{
  result->data = MatrixMultiply<3, 3, 1>(a.data, vec.data);
}

Matrix33 Matrix33::Inverted() const
{
  const auto m = [this](int x, int y) { return data[y + x * 3]; };

  const auto det = m(0, 0) * (m(1, 1) * m(2, 2) - m(2, 1) * m(1, 2)) -
                   m(0, 1) * (m(1, 0) * m(2, 2) - m(1, 2) * m(2, 0)) +
                   m(0, 2) * (m(1, 0) * m(2, 1) - m(1, 1) * m(2, 0));

  const auto invdet = 1 / det;

  Matrix33 result;

  const auto minv = [&result](int x, int y) -> auto& { return result.data[y + x * 3]; };

  minv(0, 0) = (m(1, 1) * m(2, 2) - m(2, 1) * m(1, 2)) * invdet;
  minv(0, 1) = (m(0, 2) * m(2, 1) - m(0, 1) * m(2, 2)) * invdet;
  minv(0, 2) = (m(0, 1) * m(1, 2) - m(0, 2) * m(1, 1)) * invdet;
  minv(1, 0) = (m(1, 2) * m(2, 0) - m(1, 0) * m(2, 2)) * invdet;
  minv(1, 1) = (m(0, 0) * m(2, 2) - m(0, 2) * m(2, 0)) * invdet;
  minv(1, 2) = (m(1, 0) * m(0, 2) - m(0, 0) * m(1, 2)) * invdet;
  minv(2, 0) = (m(1, 0) * m(2, 1) - m(2, 0) * m(1, 1)) * invdet;
  minv(2, 1) = (m(2, 0) * m(0, 1) - m(0, 0) * m(2, 1)) * invdet;
  minv(2, 2) = (m(0, 0) * m(1, 1) - m(1, 0) * m(0, 1)) * invdet;

  return result;
}

Matrix44 Matrix44::Identity()
{
  Matrix44 mtx = {};
  mtx.data[0] = 1.0f;
  mtx.data[5] = 1.0f;
  mtx.data[10] = 1.0f;
  mtx.data[15] = 1.0f;
  return mtx;
}

Matrix44 Matrix44::FromMatrix33(const Matrix33& m33)
{
  Matrix44 mtx;
  for (int i = 0; i < 3; ++i)
  {
    for (int j = 0; j < 3; ++j)
    {
      mtx.data[i * 4 + j] = m33.data[i * 3 + j];
    }
  }

  for (int i = 0; i < 3; ++i)
  {
    mtx.data[i * 4 + 3] = 0;
    mtx.data[i + 12] = 0;
  }
  mtx.data[15] = 1.0f;
  return mtx;
}

Matrix44 Matrix44::FromArray(const std::array<float, 16>& arr)
{
  Matrix44 mtx;
  mtx.data = arr;
  return mtx;
}

Matrix44 Matrix44::Scale(const Vec3& vec)
{
  Matrix44 mtx = {};
  mtx.data[0] = vec.x;
  mtx.data[5] = vec.y;
  mtx.data[10] = vec.z;
  mtx.data[15] = 1.0f;
  return mtx;
}

Matrix44 Matrix44::Translate(const Vec3& vec)
{
  Matrix44 mtx = Matrix44::Identity();
  mtx.data[3] = vec.x;
  mtx.data[7] = vec.y;
  mtx.data[11] = vec.z;
  return mtx;
}

Matrix44 Matrix44::ForceZ(const float z)
{
  Matrix44 mtx = Matrix44::Identity();
  mtx.data[10] = 0.0f;
  mtx.data[11] = z;
  return mtx;
}

void Matrix44::UseFixedZ(const float z)
{
  // Instead of using the incoming z in the matrix,
  // use the argument.
  // We accomplish this by moving some z column values into the w column and multiplying.
  this->data[3] = z * this->data[2];
  this->data[2] = 0.0f;
  this->data[7] = z * this->data[6];
  this->data[6] = 0.0f;
  //this->data[11] = z * this->data[10];
  //this->data[10] = 0.0f;
  this->data[15] = z * this->data[14];
  this->data[14] = 0.0f;
  //this->data[15] = -this->data[14]; // Use z=-1 to avoid shrinking
  //this->data[14] = 0.0f;
}

void Matrix44::ForceW(const float w)
{
  this->data[14] = 0.0f;
  this->data[15] = w;
}

Matrix44 Matrix44::Shear(const float a, const float b)
{
  Matrix44 mtx = Matrix44::Identity();
  mtx.data[2] = a;
  mtx.data[6] = b;
  return mtx;
}

Matrix44 Matrix44::Perspective(float fov_y, float aspect_ratio, float z_near, float z_far)
{
  Matrix44 mtx{};
  const float tan_half_fov_y = std::tan(fov_y / 2);
  mtx.data[0] = 1 / (aspect_ratio * tan_half_fov_y);
  mtx.data[5] = 1 / tan_half_fov_y;
  mtx.data[10] = -(z_far + z_near) / (z_far - z_near);
  mtx.data[11] = -(2 * z_far * z_near) / (z_far - z_near);
  mtx.data[14] = -1;
  return mtx;
}

Matrix44 Matrix44::Frustum(float left, float right, float bottom, float top, float z_near,
                           float z_far)
{
  Matrix44 mtx{};
  mtx.data[0] = 2 * z_near / (right - left);
  mtx.data[2] = (right + left) / (right - left);
  mtx.data[5] = 2 * z_near / (top - bottom);
  mtx.data[6] = (top + bottom) / (top - bottom);
  if (z_far != -INFINITY)
  {
    mtx.data[10] = -(z_far + z_near) / (z_far - z_near);
    mtx.data[11] = -(2 * z_far * z_near) / (z_far - z_near);
  }
  else
  {
    mtx.data[10] = -1.0f;
    mtx.data[11] = 0.0f;
  }

  mtx.data[14] = -1;
  return mtx;
}

Matrix44 Matrix44::FrustumD3D(float left, float right, float bottom, float top, float z_near,
                           float z_far)
{
  Matrix44 mtx{};
  mtx.data[0] = 2 * z_near / (right - left);
  mtx.data[2] = (right + left) / (right - left);
  mtx.data[5] = 2 * z_near / (top - bottom);
  mtx.data[6] = (top + bottom) / (top - bottom);
  if (z_far != -INFINITY)
  {
    mtx.data[10] = -(z_far) / (z_far - z_near);
    mtx.data[11] = -(z_far * z_near) / (z_far - z_near);
  }
  else
  {
    mtx.data[10] = -1.0f;
    mtx.data[11] = 0.0f;
  }

  mtx.data[14] = -1;
  return mtx;
}


void Matrix44::Multiply(const Matrix44& a, const Matrix44& b, Matrix44* result)
{
  result->data = MatrixMultiply<4, 4, 4>(a.data, b.data);
}

Vec3 Matrix44::Transform(const Vec3& v, float w) const
{
  const auto result = MatrixMultiply<4, 4, 1>(data, {v.x, v.y, v.z, w});
  return Vec3{result[0], result[1], result[2]};
}

void Matrix44::Multiply(const Matrix44& a, const Vec4& vec, Vec4* result)
{
  result->data = MatrixMultiply<4, 4, 1>(a.data, vec.data);
}

// TODO: Perhaps this should return optional<>?
Matrix44 Matrix44::Inverted() const
{
  XrMatrix4x4f input_xr;
  for (int i = 0; i < 16; i++)
  {
    input_xr.m[i] = this->data[i];
  }
  XrMatrix4x4f transposed;
  XrMatrix4x4f_Transpose(&transposed, &input_xr);
  XrMatrix4x4f inverse_transposed;
  XrMatrix4x4f_Invert(&inverse_transposed, &transposed);
  XrMatrix4x4f inverse_xr;
  XrMatrix4x4f_Transpose(&inverse_xr, &inverse_transposed);
  Matrix44 result;
  for (int i = 0; i < 16; i++)
  {
    result.data[i] = inverse_xr.m[i];
  }
  return result;
}

}  // namespace Common
