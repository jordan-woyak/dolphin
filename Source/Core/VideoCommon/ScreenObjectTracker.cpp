// Copyright 2020 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "VideoCommon/ScreenObjectTracker.h"

#include <algorithm>

#include "Common/Logging/Log.h"
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

  if (m_new_objects.size() < m_objects.size())
    m_new_objects.insert(m_new_objects.end(), m_objects.size() - m_new_objects.size(), {});
  else if (m_objects.size() < m_new_objects.size())
    m_objects.insert(m_objects.end(), m_new_objects.size() - m_objects.size(), {});

  // TODO: maybe work in order of objects with highest power already?
  for (auto& obj : m_objects)
  {
    const auto obj_distance = [&](const Object& other) {
      if (obj.hash != other.hash)
        return std::numeric_limits<float>::infinity();
      return (obj.position - other.position).LengthSquared();
    };

    const auto cmp = [&](const Object& obj1, const Object& obj2) {
      return obj_distance(obj1) < obj_distance(obj2);
    };

    const auto closest_iter = std::min_element(m_new_objects.begin(), m_new_objects.end(), cmp);

    obj.UpdatePosition(closest_iter->position);
    // TODO: do some erase/remove trickery to prevent large amounts of moving in vector.
    m_new_objects.erase(closest_iter);
  }

  // debugging

  if (m_objects.empty())
    return;

  const auto cmp = [&](const Object& obj1, const Object& obj2) {
    return obj1.stored_power < obj2.stored_power;
  };

  const auto biggest_power_iter = std::max_element(m_objects.begin(), m_objects.end(), cmp);

  INFO_LOG(WIIMOTE, "total objects: %d", m_objects.size());
  INFO_LOG(WIIMOTE, "highest object magnitude: %f", biggest_power_iter->stored_power);
}

ScreenObjectTracker::Object::Object()
{
  filter1.SetNormalizedFrequency(7 / 60.f);
  filter1.SetNormalizedFrequency(5 / 60.f);
}

void ScreenObjectTracker::Object::UpdatePosition(Common::Vec3 new_position)
{
  if (!starting_position.has_value())
    starting_position = new_position;

  const auto sample = (new_position.x - starting_position->x) / EFB_WIDTH;

  filter1.AddSample(sample);
  filter2.AddSample(sample);

  // INFO_LOG(WIIMOTE, "object power: %f", power.GetPower());

  // INFO_LOG(WIIMOTE, "object movement: %f", new_position.x - starting_position.x);

  position = new_position;

  if (filter1.GetSampleCount() >= 100)
  {
    // stored_power = std::min(filter1.GetMagnitude(), filter2.GetMagnitude());
    stored_power = filter1.GetMagnitude();
    filter1.Reset();
    filter2.Reset();

    if (position.x > EFB_WIDTH || position.x < 0 || position.y > EFB_HEIGHT || position.y < 0)
      stored_power = 0;

    // TODO: this should happen on construction..
    starting_position = position;
  }
}

void ScreenObjectTracker::AddObject(Common::Vec3 position)
{
  Object obj;
  obj.position = position;
  obj.hash = m_current_hash;
  m_new_objects.emplace_back(obj);
}

void ScreenObjectTracker::SetCurrentHash(Hash hash)
{
  m_current_hash = hash;
}
