// Copyright 2020 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "VideoCommon/ScreenObjectTracker.h"

#include <complex>

#include "Common/Logging/Log.h"
#include "Common/MathUtil.h"

// calculating revers number
int reverse(int N, int n)
{
  int j, p = 0;
  for (j = 1; j <= IntLog2(N); j++)
  {
    if (n & (1 << (IntLog2(N) - j)))
      p |= 1 << (j - 1);
  }
  return p;
}

// using the reverse order in the array
void ordina(std::complex<double>* f1, int N)
{
  static constexpr int MAX = 200;

  std::complex<double> f2[MAX];
  for (int i = 0; i < N; i++)
    f2[i] = f1[reverse(N, i)];
  for (int j = 0; j < N; j++)
    f1[j] = f2[j];
}

void transform(std::complex<double>* f, int N)
{
  // first: reverse order
  ordina(f, N);
  std::vector<std::complex<double>> W(N / 2);
  W[1] = std::polar(1., -1. * MathUtil::TAU / N);
  W[0] = 1;
  for (int i = 2; i < N / 2; i++)
    W[i] = std::pow(W[1], i);
  int n = 1;
  int a = N / 2;
  for (int j = 0; j < log2(N); j++)
  {
    for (int i = 0; i < N; i++)
    {
      if (!(i & n))
      {
        const auto temp = f[i];
        const auto Temp = W[(i * a) % (n * a)] * f[i + n];
        f[i] = temp + Temp;
        f[i + n] = temp - Temp;
      }
    }
    n *= 2;
    a = a / 2;
  }
}

void FFT(std::complex<double>* f, int N, double d)
{
  transform(f, N);
  for (int i = 0; i < N; i++)
    f[i] *= d;
}

void ScreenObjectTracker::OnFrameAdvance()
{
  INFO_LOG(WIIMOTE, "tracked objects: %d", m_objects.size());
  m_objects.clear();
}

void ScreenObjectTracker::AddObject(Common::Vec3 position)
{
  Object obj;
  obj.position = position;
  m_objects.emplace_back(obj);
}
