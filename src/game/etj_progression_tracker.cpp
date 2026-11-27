/*
 * MIT License
 *
 * Copyright (c) 2026 ETJump team <zero@etjump.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "etj_progression_tracker.h"

#include <bitset>

#include "etj_printer.h"
#include "etj_session.h"
#include "etj_string_utilities.h"

namespace ETJump {
const char *ProgressionTrackers::ETJUMP_PROGRESSION_TRACKER_VALUE_NOT_SET = "-1";

ProgressionTrackers::ProgressionTrackers() {
  _progressionTrackers.clear();
}

ProgressionTrackers::~ProgressionTrackers() {}

void ProgressionTrackers::printParserErrors(const std::vector<std::string> &errors, const std::string &text) {
  auto buffer  = "Tracker parse error on line: " + text + "\n";
  buffer      += StringUtils::join(errors, "\n");
  G_Error(buffer.c_str());
}

void ProgressionTrackers::updateTracker(std::vector<ProgressionTrackerParser::IndexValuePair> pairs,
                                        int tracker[MAX_PROGRESSION_TRACKERS]) {
  for (const auto &pair : pairs) {
    if (pair.index >= MAX_PROGRESSION_TRACKERS) {
      G_Error("Tracker error: specified index (%d) is "
              "greater than maximum "
              "number of trackers indices (%d)",
              pair.index + 1, MAX_PROGRESSION_TRACKERS);
    }

    tracker[pair.index] = pair.value;
  }
}

std::vector<ProgressionTrackerParser::IndexValuePair> ProgressionTrackers::parseKey(const std::string &key) {
  auto parser = ProgressionTrackerParser(key);
  auto errors = parser.getErrors();
  if (errors.size()) {
    printParserErrors(errors, key);
  }
  return parser.getParsedPairs();
}

int ProgressionTrackers::registerTracker(ProgressionTrackerKeys keys) {
  auto progressionTracker = ProgressionTracker();

  updateTracker(parseKey(keys.equal), progressionTracker.equal);
  updateTracker(parseKey(keys.notEqualAny), progressionTracker.notEqualAny);
  updateTracker(parseKey(keys.notEqualAll), progressionTracker.notEqualAll);
  updateTracker(parseKey(keys.greaterThan), progressionTracker.greaterThan);
  updateTracker(parseKey(keys.lessThan), progressionTracker.lessThan);
  updateTracker(parseKey(keys.set), progressionTracker.set);
  updateTracker(parseKey(keys.setIf), progressionTracker.setIf);
  updateTracker(parseKey(keys.increment), progressionTracker.increment);
  updateTracker(parseKey(keys.incrementIf), progressionTracker.incrementIf);
  updateTracker(parseKey(keys.bitIsSet), progressionTracker.bitIsSet);
  updateTracker(parseKey(keys.bitNotSet), progressionTracker.bitNotSet);
  updateTracker(parseKey(keys.bitSet), progressionTracker.bitSet);
  updateTracker(parseKey(keys.bitReset), progressionTracker.bitReset);

  _progressionTrackers.push_back(progressionTracker);
  return _progressionTrackers.size() - 1;
}

void ProgressionTrackers::useTracker(gentity_t *ent, gentity_t *activator, const ProgressionTracker &tracker) {
  std::array<int32_t, MAX_PROGRESSION_TRACKERS> oldValues{};

  if (g_debugTrackers.integer) {
    memcpy(oldValues.data(), activator->client->pers.progression, sizeof(oldValues));
  }

  auto idx = 0;

  for (auto &v : tracker.set) {
    if (v >= 0) {
      activator->client->pers.progression[idx] = v;
    }

    ++idx;
  }

  idx = 0;
  for (auto &v : tracker.increment) {
    if (v != 0) {
      activator->client->pers.progression[idx] += v;
    }
    ++idx;
  }

  idx = 0;
  for (auto &v : tracker.bitSet) {
    if (v >= 0) {
      activator->client->pers.progression[idx] |= (1 << v);
    }
    ++idx;
  }

  idx = 0;
  for (auto &v : tracker.bitReset) {
    if (v >= 0) {
      activator->client->pers.progression[idx] &= ~(1 << v);
    }
    ++idx;
  }

  auto activate                 = true;
  uint8_t numNotEqualAllKeys    = 0;
  uint8_t numNotEqualAllMatches = 0;

  // FIXME: I really, really do not like this loop. This is very error prone
  //  for mappers, because they can specify keys which cancel out each others
  //  functionality with seemingly no logic, and no error messages whatsoever.
  //  Logically you should never specify multiple of these keys, and that's what
  //  this assumes, but there's nothing stopping mappers from doing that for
  //  any other keys besides 'tracker_not_eq_any' and 'tracker_not_eq_all'.
  for (idx = 0; idx < MAX_PROGRESSION_TRACKERS; ++idx) {
    const int clientTracker = activator->client->pers.progression[idx];

    if ((tracker.equal[idx] != ProgressionTrackerValueNotSet && tracker.equal[idx] != clientTracker) ||
        (tracker.lessThan[idx] != ProgressionTrackerValueNotSet && tracker.lessThan[idx] <= clientTracker) ||
        (tracker.greaterThan[idx] != ProgressionTrackerValueNotSet && tracker.greaterThan[idx] >= clientTracker) ||
        (tracker.notEqualAny[idx] != ProgressionTrackerValueNotSet && tracker.notEqualAny[idx] == clientTracker)) {
      activate = false;
      break;
    }

    if (tracker.notEqualAll[idx] != ProgressionTrackerValueNotSet) {
      numNotEqualAllKeys++;

      if (tracker.notEqualAll[idx] == clientTracker) {
        numNotEqualAllMatches++;
      }
    }
  }

  if (numNotEqualAllKeys > 0 && numNotEqualAllKeys == numNotEqualAllMatches) {
    activate = false;
  }

  if (activate) {
    for (idx = 0; idx < MAX_PROGRESSION_TRACKERS; ++idx) {
      const auto clientBits = std::bitset<32>(activator->client->pers.progression[idx]);

      if (tracker.bitIsSet[idx] != ProgressionTrackerValueNotSet && !clientBits.test(tracker.bitIsSet[idx])) {
        activate = false;
        break;
      }

      if (tracker.bitNotSet[idx] != ProgressionTrackerValueNotSet && clientBits.test(tracker.bitNotSet[idx])) {
        activate = false;
        break;
      }
    }
  }

  if (!activate && g_debugTrackers.integer) {
    printUnmetRequirements(ent, activator, tracker);
  }

  if (activate) {
    G_UseTargetedEntities(ent, activator);

    for (idx = 0; idx < MAX_PROGRESSION_TRACKERS; ++idx) {
      if (tracker.setIf[idx] >= 0) {
        activator->client->pers.progression[idx] = tracker.setIf[idx];
      }

      if (tracker.incrementIf[idx] != 0) {
        activator->client->pers.progression[idx] += tracker.incrementIf[idx];
      }
    }

    if (g_debugTrackers.integer) {
      printTrackerChanges(activator, oldValues);
    }
  }
}

void ProgressionTrackers::printTrackerChanges(const gentity_t *activator,
                                              const std::array<int32_t, MAX_PROGRESSION_TRACKERS> &oldValues) {
  const auto clientNum = ClientNum(activator);

  for (int i = 0; i < static_cast<int>(oldValues.size()); i++) {
    if (oldValues[i] != activator->client->pers.progression[i]) {
      const std::string &trackerChangeMsg =
        StringUtils::format("^7Tracker change - index: ^3%i ^7value: ^2%i ^7from: ^9%i^7\n", i + 1,
                            activator->client->pers.progression[i], oldValues[i]);
      Printer::popup(clientNum, trackerChangeMsg);
    }
  }
}

void ProgressionTrackers::printUnmetRequirements(const gentity_t *ent, const gentity_t *activator,
                                                 const ProgressionTracker &tracker) {
  const int32_t clientNum = ClientNum(activator);
  UnmetPrint &last        = lastUnmetPrint[clientNum];

  if (last.entityNum == ent->s.number && level.time >= last.time && level.time - last.time < 1000) {
    return;
  }

  last = {ent->s.number, level.time};

  const int *progression = activator->client->pers.progression;
  std::vector<std::string> unmet;
  std::vector<std::string> notEqualAll;
  bool notEqualAllMet = false;

  const auto requirement = [progression](const char *key, const int32_t idx, const int32_t required) {
    return StringUtils::format("^7Tracker ^3%i^7: ^9%s %i ^7not met, value: ^2%i", idx + 1, key, required,
                               progression[idx]);
  };

  const auto bitIsSet = [](const int32_t value, const int32_t bit) {
    return bit >= 0 && bit < 32 && (value & (1 << bit));
  };

  for (int32_t idx = 0; idx < MAX_PROGRESSION_TRACKERS; idx++) {
    const int32_t value = progression[idx];

    if (tracker.equal[idx] != ProgressionTrackerValueNotSet && tracker.equal[idx] != value) {
      unmet.emplace_back(requirement("tracker_eq", idx, tracker.equal[idx]));
    }

    if (tracker.notEqualAny[idx] != ProgressionTrackerValueNotSet && tracker.notEqualAny[idx] == value) {
      unmet.emplace_back(requirement("tracker_not_eq_any", idx, tracker.notEqualAny[idx]));
    }

    if (tracker.greaterThan[idx] != ProgressionTrackerValueNotSet && tracker.greaterThan[idx] >= value) {
      unmet.emplace_back(requirement("tracker_gt", idx, tracker.greaterThan[idx]));
    }

    if (tracker.lessThan[idx] != ProgressionTrackerValueNotSet && tracker.lessThan[idx] <= value) {
      unmet.emplace_back(requirement("tracker_lt", idx, tracker.lessThan[idx]));
    }

    if (tracker.bitIsSet[idx] != ProgressionTrackerValueNotSet && !bitIsSet(value, tracker.bitIsSet[idx])) {
      unmet.emplace_back(requirement("tracker_bit_is_set", idx, tracker.bitIsSet[idx]));
    }

    if (tracker.bitNotSet[idx] != ProgressionTrackerValueNotSet && bitIsSet(value, tracker.bitNotSet[idx])) {
      unmet.emplace_back(requirement("tracker_bit_not_set", idx, tracker.bitNotSet[idx]));
    }

    // only unmet if every index matches
    if (tracker.notEqualAll[idx] != ProgressionTrackerValueNotSet) {
      if (tracker.notEqualAll[idx] == value) {
        notEqualAll.emplace_back(requirement("tracker_not_eq_all", idx, tracker.notEqualAll[idx]));
      } else {
        notEqualAllMet = true;
      }
    }
  }

  if (!notEqualAllMet) {
    unmet.insert(unmet.end(), notEqualAll.cbegin(), notEqualAll.cend());
  }

  Printer::popup(clientNum, StringUtils::format("^7%s ^3%s ^7requirements not met:\n", ent->classname,
                                                ent->targetname ? ent->targetname : ""));

  for (const auto &msg : unmet) {
    Printer::popup(clientNum, msg + "\n");
  }
}

void ProgressionTrackers::useTriggerTracker(gentity_t *ent, gentity_t *activator) {
  const auto &tracker = _progressionTrackers[ent->key];

  if (!activator || !activator->client) {
    return;
  }

  useTracker(ent, activator, tracker);
}

void ProgressionTrackers::useTargetTracker(gentity_t *ent, gentity_t *other, gentity_t *activator) {
  const auto &tracker = _progressionTrackers[ent->key];

  if (!activator || !activator->client) {
    return;
  }

  useTracker(ent, activator, tracker);
}

void ProgressionTrackers::targetTrackerSpawn(gentity_t *self) {
  const auto keys          = ProgressionTrackers::ParseTrackerKeys();
  const char *defaultValue = ProgressionTrackers::ETJUMP_PROGRESSION_TRACKER_VALUE_NOT_SET;

  if (Q_stricmp(keys.notEqualAny, defaultValue) && Q_stricmp(keys.notEqualAll, defaultValue)) {
    G_Error("%s: 'tracker_not_eq_any' and 'tracker_not_eq_all' cannot be used "
            "in the same tracker entity.",
            __func__);
  }

  self->key = progressionTrackers->registerTracker(keys);
  self->use = [](gentity_t *ent, gentity_t *other, gentity_t *activator) {
    progressionTrackers->useTargetTracker(ent, other, activator);
  };
}

void ProgressionTrackers::triggerTrackerSpawn(gentity_t *self) {
  InitTrigger(self);
  // just make it same type as trigger multiple for now
  self->s.eType = ET_TRIGGER_MULTIPLE;

  const auto keys          = ProgressionTrackers::ParseTrackerKeys();
  const char *defaultValue = ProgressionTrackers::ETJUMP_PROGRESSION_TRACKER_VALUE_NOT_SET;

  if (Q_stricmp(keys.notEqualAny, defaultValue) && Q_stricmp(keys.notEqualAll, defaultValue)) {
    G_Error("%s: 'tracker_not_eq_any' and 'tracker_not_eq_all' cannot be used "
            "in the same tracker entity.",
            __func__);
  }

  self->key = progressionTrackers->registerTracker(keys);
  self->use = [](gentity_t *ent, gentity_t *other, gentity_t *activator) {
    progressionTrackers->useTriggerTracker(ent, activator);
  };
  self->touch = [](gentity_t *ent, gentity_t *activator, trace_t *trace) {
    progressionTrackers->useTriggerTracker(ent, activator);
  };
}

ProgressionTrackers::ProgressionTrackerKeys ProgressionTrackers::ParseTrackerKeys() {
  ProgressionTrackers::ProgressionTrackerKeys keys{};

  const char *VALUE_NOT_SET = ProgressionTrackers::ETJUMP_PROGRESSION_TRACKER_VALUE_NOT_SET;

  G_SpawnString("tracker_eq", VALUE_NOT_SET, &keys.equal);

  // legacy 'tracker_not_eq', superseded by 'tracker_not_eq_any'
  // kept here for backwards compatibility with old maps
  if (!G_SpawnString("tracker_not_eq", VALUE_NOT_SET, &keys.notEqualAny)) {
    G_SpawnString("tracker_not_eq_any", VALUE_NOT_SET, &keys.notEqualAny);
  }

  G_SpawnString("tracker_not_eq_all", VALUE_NOT_SET, &keys.notEqualAll);
  G_SpawnString("tracker_gt", VALUE_NOT_SET, &keys.greaterThan);
  G_SpawnString("tracker_lt", VALUE_NOT_SET, &keys.lessThan);
  G_SpawnString("tracker_set", VALUE_NOT_SET, &keys.set);
  G_SpawnString("tracker_set_if", VALUE_NOT_SET, &keys.setIf);
  G_SpawnString("tracker_inc", "0", &keys.increment);
  G_SpawnString("tracker_inc_if", "0", &keys.incrementIf);
  G_SpawnString("tracker_bit_is_set", VALUE_NOT_SET, &keys.bitIsSet);
  G_SpawnString("tracker_bit_not_set", VALUE_NOT_SET, &keys.bitNotSet);
  G_SpawnString("tracker_bit_set", VALUE_NOT_SET, &keys.bitSet);
  G_SpawnString("tracker_bit_reset", VALUE_NOT_SET, &keys.bitReset);

  return keys;
}

void ProgressionTrackers::saveClientProgression(const gentity_t *ent) {
  const std::string guid = session->Guid(ent);
  std::copy(std::begin(ent->client->pers.progression), std::end(ent->client->pers.progression),
            savedProgression[guid].begin());
}

void ProgressionTrackers::restoreClientProgression(const gentity_t *ent) {
  const std::string guid = session->Guid(ent);

  if (savedProgression.find(guid) != savedProgression.cend()) {
    std::memcpy(ent->client->pers.progression, savedProgression[guid].data(), sizeof(ent->client->pers.progression));
  }
}

} // namespace ETJump
