/*
   Copyright 2023 NXP

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
 */

#include <chrono>
#include <cmath>
#include <cstdint>
#include <stdio.h>
#include <stdlib.h>
#include <string>


namespace pf {

class ProfilePoint
{
public:
    ProfilePoint(std::string _name, uint64_t _reportPeriodMs):
                    name(_name), reportPeriodMs(_reportPeriodMs) {
        resetPeriod();
    }
    virtual ~ProfilePoint() {}

    void enter() {
        pointStart = std::chrono::system_clock::now();
    }

    void exit() {
        std::chrono::system_clock::time_point pointExit;
        uint64_t pointDurationUs;
        pointExit = std::chrono::system_clock::now();
        pointDurationUs = std::chrono::duration_cast<std::chrono::microseconds>
                            (pointExit - pointStart).count();
        pointDurationUsAcc += pointDurationUs;
        if (pointDurationUs < pointDurationUsMin)
            pointDurationUsMin = pointDurationUs;
        if (pointDurationUs > pointDurationUsMax)
            pointDurationUsMax = pointDurationUs;
        pointCount++;

        uint64_t periodDuration = std::chrono::duration_cast<std::chrono::milliseconds>
                            (pointExit - periodStart).count();
        if (periodDuration > reportPeriodMs) {
            float mean = (float)pointDurationUsAcc / 1000.0f / pointCount;
            float min = pointDurationUsMin / 1000.0f;
            float max  = pointDurationUsMax / 1000.0f;
            float rate  = pointCount * 1000.0f / periodDuration;

            printf("PF(%s) mean(ms):%.1f min(ms):%.1f max(ms):%.1f rate(/s):%.1f\n",
                name.c_str(), mean, min, max, rate);
            resetPeriod();
        }
    }

private:
    void resetPeriod() {
        periodStart = std::chrono::system_clock::now();
        pointCount = 0ULL;
        pointDurationUsAcc = 0ULL;
        pointDurationUsMin = UINT64_MAX;
        pointDurationUsMax = 0ULL;
    }
    std::chrono::system_clock::time_point periodStart;
    std::chrono::system_clock::time_point pointStart;
    std::string name;
    uint64_t reportPeriodMs;
    uint64_t pointCount;
    uint64_t pointDurationUsAcc;
    uint64_t pointDurationUsMin;
    uint64_t pointDurationUsMax;
};

} //namespace


#ifdef PF_ENABLED

#ifndef PF_REPORTING_PERIOD_MS
    #define PF_REPORTING_PERIOD_MS 5000
#endif

#define PF_ENTRY_NAME(name) pf_entry_##name

#define PF_ENTRY(name) \
    static pf::ProfilePoint PF_ENTRY_NAME(name)(#name, PF_REPORTING_PERIOD_MS)

#define PF_ENTRY_PERIOD_MS(name, period) \
    static pf::ProfilePoint PF_ENTRY_NAME(name)(#name, period)

#define PF_ENTER(name) \
    PF_ENTRY_NAME(name).enter()

#define PF_EXIT(name) \
    PF_ENTRY_NAME(name).exit()

#else

#define PF_ENTRY(name)
#define PF_ENTRY_PERIOD_MS(name, period)
#define PF_ENTER(name)
#define PF_EXIT(name)

#endif
