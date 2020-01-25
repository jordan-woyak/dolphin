// Copyright 2019 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cmath>
#include <functional>
#include <type_traits>

// Tiny matrix/vector library.
// Used for things like Free-Look in the gfx backend.

namespace Common
{
template <typename T, size_t N>
struct TVecN;

namespace VecDetail
{
template <size_t N, typename T>
auto& Get(T& value)
{
  if constexpr (std::is_arithmetic_v<T>)
    return value;
  else
    return value.data[N];
}

template <typename T, size_t N>
struct VecMembers;

template <typename T>
struct VecMembers<T, 2>
{
  template <typename OtherT>
  auto Cross(const TVecN<OtherT, 2>& rhs) const
  {
    return (x * rhs.y) - (y * rhs.x);
  }

  union
  {
    std::array<T, 2> data;
    struct
    {
      T x, y;
    };
  };
};
template <typename T>
struct VecMembers<T, 3>
{
  template <typename OtherT>
  auto Cross(const TVecN<OtherT, 3>& rhs) const
  {
    // TODO: take advantage of CTAD. using base ctors maybe?
    return TVecN<decltype(Get<0>(*this) * Get<0>(rhs)), 3>(
        (y * rhs.z) - (rhs.y * z), (z * rhs.x) - (rhs.z * x), (x * rhs.y) - (rhs.x * y));
  }

  union
  {
    std::array<T, 3> data;
    struct
    {
      T x, y, z;
    };
  };
};
template <typename T>
struct VecMembers<T, 4>
{
  union
  {
    std::array<T, 4> data;
    struct
    {
      T x, y, z, w;
    };
  };
};

template <typename T, size_t... Is>
struct VecFunctions : VecMembers<T, sizeof...(Is)>
{
  using value_type = T;
  static constexpr size_t size = sizeof...(Is);

private:
  using VecType = TVecN<value_type, size>;

public:
  auto Dot(const VecType& other) const { return ((Get<Is>(*this) * Get<Is>(other)) + ...); }

  template <typename... Args>
  void Assign(Args&&... args)
  {
    static_assert(sizeof...(args) == size);

    ((Get<Is>(*this) = args), ...);
  }

  auto operator-() const { return VecType(Get<Is>(*this)...); }

  template <typename Other>
  auto operator*=(const Other& rhs)
  {
    return *this * rhs;
  }

  template <typename Other>
  auto operator*(const Other& rhs) const
  {
    return TVecN{(Get<Is>(*this) * Get<Is>(rhs))...};
  }

  template <typename Other>
  auto operator/=(const Other& rhs)
  {
    return *this / rhs;
  }

  template <typename Other>
  auto operator/(const Other& rhs) const
  {
    return TVecN{(Get<Is>(*this) / Get<Is>(rhs))...};
  }

  template <typename OtherT, size_t OtherN>
  auto operator+=(const TVecN<OtherT, OtherN>& rhs)
  {
    return *this + rhs;
  }

  template <typename OtherT, size_t OtherN>
  auto operator+(const TVecN<OtherT, OtherN>& rhs) const
  {
    return TVecN{(Get<Is>(*this) + Get<Is>(rhs))...};
  }

  template <typename OtherT, size_t OtherN>
  auto operator-=(const TVecN<OtherT, OtherN>& rhs)
  {
    return *this - rhs;
  }

  template <typename OtherT, size_t OtherN>
  auto operator-(const TVecN<OtherT, OtherN>& rhs) const
  {
    return TVecN{(Get<Is>(*this) - Get<Is>(rhs))...};
  }

  template <typename OtherT, size_t OtherN>
  auto operator<(const TVecN<OtherT, OtherN>& rhs) const
  {
    return TVecN{(Get<Is>(*this) < Get<Is>(rhs))...};
  }
};

template <typename T, size_t N>
struct VecImpl;

template <typename T>
struct VecImpl<T, 2> : VecFunctions<T, 0, 1>
{
};
template <typename T>
struct VecImpl<T, 3> : VecFunctions<T, 0, 1, 2>
{
};
template <typename T>
struct VecImpl<T, 4> : VecFunctions<T, 0, 1, 2, 3>
{
};

};  // namespace VecDetail

template <typename T, size_t N>
struct TVecN : VecDetail::VecImpl<T, N>
{
  TVecN() = default;

  ~TVecN() { static_assert(sizeof(*this) == sizeof(typename TVecN::value_type) * TVecN::size); }

  template <typename OtherT, size_t OtherN, typename... Args>
  TVecN(const TVecN<OtherT, OtherN>& other, const Args&... args)
  {
    // TODO:
  }

  template <typename... Args, typename = std::enable_if_t<(std::is_arithmetic_v<Args> && ...)>>
  TVecN(const Args&... args)
  {
    this->Assign(args...);
  }

  auto LengthSquared() const { return this->Dot(*this); }
  auto Length() { return std::sqrt(LengthSquared()); }
  auto Normalized() const { return *this / Length(); }
};

// TODO: ensure all T are the same.
template <typename... T>
TVecN(T&&...)->TVecN<std::common_type_t<T...>, sizeof...(T)>;

template <typename T>
using TVec2 = TVecN<T, 2>;
using Vec2 = TVec2<float>;
using DVec2 = TVec2<double>;

template <typename T>
using TVec3 = TVecN<T, 3>;
using Vec3 = TVec3<float>;
using DVec3 = TVec3<double>;

template <typename T>
using TVec4 = TVecN<T, 4>;
using Vec4 = TVec4<float>;
using DVec4 = TVec4<double>;

class Matrix33
{
public:
  static Matrix33 Identity();
  static Matrix33 FromQuaternion(float x, float y, float z, float w);

  // Return a rotation matrix around the x,y,z axis
  static Matrix33 RotateX(float rad);
  static Matrix33 RotateY(float rad);
  static Matrix33 RotateZ(float rad);

  static Matrix33 Rotate(float rad, const Vec3& axis);

  static Matrix33 Scale(const Vec3& vec);

  // set result = a x b
  static void Multiply(const Matrix33& a, const Matrix33& b, Matrix33* result);
  static void Multiply(const Matrix33& a, const Vec3& vec, Vec3* result);

  Matrix33 Inverted() const;

  Matrix33& operator*=(const Matrix33& rhs)
  {
    Multiply(*this, rhs, this);
    return *this;
  }

  // Note: Row-major storage order.
  std::array<float, 9> data;
};

inline Matrix33 operator*(Matrix33 lhs, const Matrix33& rhs)
{
  return lhs *= rhs;
}

inline Vec3 operator*(const Matrix33& lhs, Vec3 rhs)
{
  Matrix33::Multiply(lhs, rhs, &rhs);
  return rhs;
}

class Matrix44
{
public:
  static Matrix44 Identity();
  static Matrix44 FromMatrix33(const Matrix33& m33);
  static Matrix44 FromArray(const std::array<float, 16>& arr);

  static Matrix44 Translate(const Vec3& vec);
  static Matrix44 Shear(const float a, const float b = 0);
  static Matrix44 Perspective(float fov_y, float aspect_ratio, float z_near, float z_far);

  static void Multiply(const Matrix44& a, const Matrix44& b, Matrix44* result);
  static void Multiply(const Matrix44& a, const Vec4& vec, Vec4* result);

  // For when a vec4 isn't needed a multiplication function that takes a Vec3 and w:
  Vec3 Transform(const Vec3& point, float w) const;

  Matrix44& operator*=(const Matrix44& rhs)
  {
    Multiply(*this, rhs, this);
    return *this;
  }

  // Note: Row-major storage order.
  std::array<float, 16> data;
};

inline Matrix44 operator*(Matrix44 lhs, const Matrix44& rhs)
{
  return lhs *= rhs;
}

inline Vec4 operator*(const Matrix44& lhs, Vec4 rhs)
{
  Matrix44::Multiply(lhs, rhs, &rhs);
  return rhs;
}

}  // namespace Common
