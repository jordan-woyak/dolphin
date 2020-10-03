// Copyright 2020 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "VideoCommon/ScreenObjectTracker.h"

#include <algorithm>

#include "Common/Logging/Log.h"
#include "Core/HW/VideoInterface.h"
#include "VideoCommon/VideoCommon.h"

void ScreenObjectTracker::OnFrameAdvance()
{
#if 0
  if (m_new_objects.size() == m_objects.size())
  
{
    // assume objects are in same order.
    for (std::size_t i = 0; i != m_new_objects.size(); ++i)
      m_objects[i].UpdatePosition(m_new_objects[i].position);

    m_new_objects.clear();
  }
#endif

  // if (m_new_objects.size() < m_objects.size())
  //   m_new_objects.insert(m_new_objects.end(), m_objects.size() - m_new_objects.size(), {});
  // else if (m_objects.size() < m_new_objects.size())
  //   m_objects.insert(m_objects.end(), m_new_objects.size() - m_objects.size(), {});

  // TODO: object matching and hash comparison is broken.

  // TODO: maybe work in order of objects with highest power already?
  for (auto& obj : m_new_objects)
  {
    if (m_objects.empty())
      break;

    const auto obj_distance = [&](const Object& other) {
      if (obj.hash != other.hash)
        return std::numeric_limits<float>::infinity();
      return (obj.position - other.position).LengthSquared();

      // TODO: we could potentially compare the sizes of the objects as well.
    };

    const auto cmp = [&](const Object& obj1, const Object& obj2) {
      return obj_distance(obj1) < obj_distance(obj2);
    };

    const auto closest_iter = std::min_element(m_objects.begin(), m_objects.end(), cmp);

    if (closest_iter->hash == obj.hash)
    {
      closest_iter->UpdatePosition(obj.position);
      obj = std::move(*closest_iter);

      // TODO: do some erase/remove trickery to prevent large amounts of moving in vector.
      m_objects.erase(closest_iter);
    }
  }

  // for (auto& obj : m_new_objects)
  // m_objects.emplace_back(std::move(obj));

  m_objects = std::move(m_new_objects);

  // debugging

  if (m_objects.empty())
    return;

  const auto cmp = [&](const Object& obj1, const Object& obj2) {
    return obj1.stored_power < obj2.stored_power;
  };

  const auto biggest_power_iter = std::max_element(m_objects.begin(), m_objects.end(), cmp);

  // INFO_LOG(WIIMOTE, "total objects: %d", m_objects.size());
  INFO_LOG(WIIMOTE, "highest object magnitude: %f pos: %f", biggest_power_iter->stored_power,
           biggest_power_iter->position.y);
}

ScreenObjectTracker::Object::Object()
{
  // TODO: needs update on refresh rate change..
  filter1.SetNormalizedFrequency(7 / VideoInterface::GetTargetRefreshRate());
  filter2.SetNormalizedFrequency(5 / VideoInterface::GetTargetRefreshRate());
}

void ScreenObjectTracker::Object::UpdatePosition(Common::Vec3 new_position)
{
  base_position += (new_position - base_position) * 0.01f;

  auto sample = new_position.x - base_position.x;

  if (std::abs(sample) > 0.2)
    sample = 0;

  // TODO: rename push?
  filter1.AddSample(sample);
  filter2.AddSample(sample);

  // INFO_LOG(WIIMOTE, "object power: %f", power.GetPower());

  // const auto movement = new_position.x - base_position->x;
  // if (movement > 0.01)
  //  INFO_LOG(WIIMOTE, "object movement: %f", movement);

  position = new_position;

  if (filter1.GetSampleCount() >= 120)
  {
    stored_power = std::min(filter1.GetMagnitude(), filter2.GetMagnitude());
    // stored_power = filter1.GetMagnitude();
    filter1.Reset();
    filter2.Reset();
  }
}

void ScreenObjectTracker::AddObject(Common::Vec3 position)
{
  Object obj;
  obj.base_position = position;
  obj.position = position;
  obj.hash = m_current_hash;
  m_new_objects.emplace_back(obj);
}

void ScreenObjectTracker::SetCurrentHash(Hash hash)
{
  m_current_hash = hash;
}
