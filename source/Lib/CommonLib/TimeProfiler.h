/* -----------------------------------------------------------------------------
The copyright in this software is being made available under the Clear BSD
License, included below. No patent rights, trademark rights and/or 
other Intellectual Property Rights other than the copyrights concerning 
the Software are granted under this license.

The Clear BSD License

Copyright (c) 2018-2024, Fraunhofer-Gesellschaft zur Förderung der angewandten Forschung e.V. & The VVdeC Authors.
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted (subject to the limitations in the disclaimer below) provided that
the following conditions are met:

     * Redistributions of source code must retain the above copyright notice,
     this list of conditions and the following disclaimer.

     * Redistributions in binary form must reproduce the above copyright
     notice, this list of conditions and the following disclaimer in the
     documentation and/or other materials provided with the distribution.

     * Neither the name of the copyright holder nor the names of its
     contributors may be used to endorse or promote products derived from this
     software without specific prior written permission.

NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY
THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.


------------------------------------------------------------------------------------------- */

/** \file     TimeProfiler.h
    \brief    profiling of run-time behavior (header)
*/
#pragma once

#include "CommonDef.h"

#if ENABLE_TIME_PROFILING || ENABLE_TIME_PROFILING_EXTENDED

#include "StatCounter.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>


#include <stack>
#include <array>
#include <memory>
#include <chrono>
#include <numeric>
#include <ostream>
#include <sstream>

namespace vvdec
{


// Make enum and strings macros, used for TimeProfiler and DTrace
#define MAKE_ENUM(VAR) VAR,
#define MAKE_STRINGS(VAR) #VAR,
#define MAKE_ENUM_AND_STRINGS(source, enumName, enumStringName) \
enum enumName { \
    source(MAKE_ENUM) \
    };\
const char* const enumStringName[] = { \
    source(MAKE_STRINGS) \
    }; \

// Here, users can add their profiling stages
#define E_TIME_PROF_STAGES(E_) \
  E_( P_NALU_SLICE_PIC_HL       ) \
  E_( P_CONTROL_PARSE_DERIVE_LL ) \
  E_( P_PARSERESIDUALS          ) \
  E_( P_INTRAPRED               ) \
  E_( P_MOTCOMP                 ) \
  E_( P_ITRANS_REC              ) \
  E_( P_DBFILTER                ) \
  E_( P_SAO                     ) \
  E_( P_RESHAPER                ) \
  E_( P_ALF                     ) \
  E_( P_OTHER                   ) \
  E_( P_STAGES                  ) \
  E_( P_VOID = P_STAGES         )
MAKE_ENUM_AND_STRINGS(E_TIME_PROF_STAGES, STAGE, stageNames)

template<class Rep>
std::ostream& operator<<( std::ostream& os, const std::chrono::duration<double, Rep> d )
{
  os << d.count();
  return os;
}

#if ENABLE_TIME_PROFILING
class TimeProfiler
{
public:
  using rep                 = std::milli;
  using clock               = std::chrono::steady_clock;
  using time_point          = std::chrono::time_point<clock>;
  using duration            = std::chrono::duration<double, rep>;

private:
  time_point m_previous = clock::now();
  STAGE    m_eStage;
  const unsigned m_numStages = P_STAGES + 1;
  int      m_iLevel;
  int      m_iExtData;
  unsigned m_numBlkHor;
  unsigned m_numBlkVer;
  unsigned m_curWId;
  unsigned m_curHId;

public:
  const time_point start_time = m_previous;
  std::vector<duration> durations;

  TimeProfiler() : m_eStage( P_VOID )
  {
    init();
  }
  void init() 
  {
    durations.resize( m_numStages );
    for( size_t i = 0; i < m_numStages; ++i ) { durations[i] = durations[i].zero(); }
  }
  TimeProfiler& operator()( STAGE s ) 
  {
    time_point now = clock::now();
    durations[m_eStage] += ( now - m_previous );
    m_previous = now;
    m_eStage = s;
    return *this;
  }
  TimeProfiler& operator+=( const TimeProfiler& other ) 
  {
    auto i1 = durations.begin();
    auto i2 = other.durations.cbegin();
    for( ; i1 != durations.end() && i2 != other.durations.cend(); ++i1, ++i2 ) 
    {
      *i1 += *i2;
    }

    return *this;
  }

  void start( STAGE s )
  {
    m_previous = clock::now();
    m_eStage = s;
  }
  void stop( STAGE s ) 
  {
    time_point now = clock::now();
    durations[m_eStage] += ( now - m_previous );
  }
  STAGE curStage() { return m_eStage; }


  friend std::ostream& operator<<( std::ostream& os, const TimeProfiler& prof )
  {
    //const TimeProfiler::duration total   = TimeProfiler::clock::now() - prof.start_time;
    const TimeProfiler::duration counted = std::accumulate(prof.durations.begin(), prof.durations.end()-1, TimeProfiler::duration{});
    const double scale = 1.0;
    const int prec = 1;
    std::stringstream ss;
    ss << std::fixed << std::setprecision(prec) << (counted / scale);
    const int ts = 1 + ss.str().size();

    os << '\n';
    os << std::setw(10) << " "
       << std::setw(30) << std::left << "stages" << std::internal
       << std::setw(ts) << "time(ms)"
       << std::setw(10) << "%"
       << '\n';

    for (size_t i=0; i < P_STAGES; ++i) 
    {
        auto v = prof.durations[i];
        if( v.count() != 0.0 )
        {
          os << std::setw( 10 ) << " "
            << std::setw( 30 ) << std::left << stageNames[i] << std::internal
            << std::fixed << std::setw( ts ) << std::setprecision( prec ) << (v / scale)/* * total*/
            << std::fixed << std::setw( 10 ) << std::setprecision( prec ) << (v / counted) * 100.0
            << '\n';
        }
    }
    os << '\n';

    os << std::setw(10) << " " 
       << std::setw(30) << std::left << "TOTAL" << std::internal
       << std::fixed << std::setw(ts) << std::setprecision(prec) << (counted / scale)/*total*/
       << std::fixed << std::setw(10) << std::setprecision(prec) << 100.00
       << '\n';

    return os;
  }

  void output( std::ostream& os )
  {
    os << *this;
  }
};

class StageTimeProfiler 
{
  STAGE m_ePrevStage;
  TimeProfiler* m_profiler;
public:
  StageTimeProfiler( TimeProfiler *pcProfiler, STAGE m_eStage )
  {
    m_profiler = pcProfiler;
    m_ePrevStage = m_profiler->curStage();
    ( *m_profiler )( m_eStage );
  }
  ~StageTimeProfiler()
  {
    ( *m_profiler )( m_ePrevStage );
  }
};
#endif

///////////////////////////////////////////////////////////////////////////////
//////////////////////////    EXTENDED TIME PROFILER  ///////////////////////// 
///////////////////////////////////////////////////////////////////////////////
#if ENABLE_TIME_PROFILING_EXTENDED
using namespace StatCounters;

class TimeProfiler2D
{
public:
  using clock               = std::chrono::steady_clock;
  using time_point          = std::chrono::time_point<clock>;

private:
  time_point m_previous = clock::now();
  const time_point m_startTime = m_previous;
  STAGE    m_stage;
  unsigned m_numX;
  unsigned m_numY;
  unsigned m_numZ;
  unsigned m_curX;
  unsigned m_curY;
  unsigned m_curZ;
  std::vector<StatCounter2DSet<double>> m_counters;

public:
  TimeProfiler2D( unsigned numX = 1, unsigned numY = 1, unsigned numZ = 1 ) : m_stage( P_VOID ), m_numX( numX ), m_numY( numY ), m_numZ( numZ ), m_curX( 0 ), m_curY( 0 ), m_curZ( 0 )
  {
    m_counters.resize( m_numZ );
    for( int i = 0; i < m_numZ; i++ )
    {
      m_counters[i].init( std::vector<std::string> { stageNames, std::end( stageNames ) }, m_numX, m_numY );
    }
  }
  void count( STAGE s, unsigned x, unsigned y, unsigned z ) 
  {
    time_point now = clock::now();
    m_counters[m_curZ][m_stage][m_curY][m_curX] += ( now - m_previous ).count();
    m_previous = now;
    m_stage    = s;
    m_curX     = x;
    m_curY     = y;
    m_curZ     = z;
  }
  void start( STAGE s )
  {
    m_previous = clock::now();
    m_stage = s;
  }
  void stop( STAGE s ) 
  {
    count( m_stage, m_curX, m_curY, m_curZ );
  }
  unsigned  numStages()  { return m_counters[0].getNumCntTypes(); }
  STAGE     curStage()   { return m_stage; }
  unsigned  curX()       { return m_curX; }
  unsigned  curY()       { return m_curY; }
  unsigned  curZ()       { return m_curZ; }

  std::vector<StatCounter2DSet<double>>&       getCountersSet()       { return m_counters; }
  const std::vector<StatCounter2DSet<double>>& getCountersSet() const { return m_counters; }
};

class StageTimeProfiler2D
{
  STAGE m_prevStage;
  int   m_prevX;
  int   m_prevY;
  int   m_prevZ;
  TimeProfiler2D* m_profiler;
public:
  StageTimeProfiler2D( TimeProfiler2D *pcProfiler, STAGE m_eStage, unsigned x, unsigned y, unsigned z ) 
  {
    m_prevStage  = pcProfiler->curStage();
    m_prevX      = pcProfiler->curX();
    m_prevY      = pcProfiler->curY();
    m_prevZ      = pcProfiler->curZ();
    m_profiler   = pcProfiler;
    pcProfiler->count( m_eStage, x, y, z );
  }
  ~StageTimeProfiler2D()
  {
    m_profiler->count( m_prevStage, m_prevX, m_prevY, m_prevZ );
  }
};
#endif

#define PROFILER_START(p,s)                                     (*(p)).start(s)
#define PROFILER_STOP(p)                                           

#if ENABLE_TIME_PROFILING
#define PROF_SCOPE_AND_STAGE_COND_0(p,s)
#define PROF_SCOPE_AND_STAGE_COND_1(p,s)                        StageTimeProfiler cScopedProfiler##s((p),(s))
#define PROF_SCOPE_AND_STAGE_COND(cond,p,s)                     PROF_SCOPE_AND_STAGE_COND_ ## cond (p,s)
#define PROFILER_SCOPE_AND_STAGE_(cond,p,s)                     PROF_SCOPE_AND_STAGE_COND(cond,p,s)

#elif ENABLE_TIME_PROFILING_EXTENDED
#define PROF_EXT_ACCUM_AND_START_NEW_SET_COND_0(p,s,t)
#define PROF_EXT_ACCUM_AND_START_NEW_SET_COND_1(p,s,t)          (*(p)).count(s,t,0,0)
#define PROF_EXT_ACCUM_AND_START_NEW_SET_COND(cond,p,s,t)       PROF_EXT_ACCUM_AND_START_NEW_SET_COND_ ## cond (p,s,t)
#define PROFILER_EXT_ACCUM_AND_START_NEW_SET(cond,p,s,t)        PROF_EXT_ACCUM_AND_START_NEW_SET_COND(cond,p,s,t)

#define PROF_SCOPE_AND_STAGE_EXT_COND_0(p,s,a,b,c)
#define PROF_SCOPE_AND_STAGE_EXT_COND_1(p,s,a,b,c)              StageTimeProfiler2D cScopedProfilerExt##s((p),(s),(a),(b),(c))
#define PROF_SCOPE_AND_STAGE_EXT_COND(cond,p,s,a,b,c)           PROF_SCOPE_AND_STAGE_EXT_COND_ ## cond (p,s,a,b,c)

#if ENABLE_TIME_PROFILING_PIC_TYPES
#define PROFILER_SCOPE_AND_STAGE_EXT2D_(cond,p,s,t,x,y,w,h)     PROF_SCOPE_AND_STAGE_EXT_COND(cond,p,s,t,0,0)
#elif ENABLE_TIME_PROFILING_CTUS_IN_PIC
#define PROFILER_SCOPE_AND_STAGE_EXT2D_(cond,p,s,t,x,y,w,h)     PROF_SCOPE_AND_STAGE_EXT_COND(cond,p,s,x,y,t)
#elif ENABLE_TIME_PROFILING_CU_SHAPES
#define PROFILER_SCOPE_AND_STAGE_EXT2D_(cond,p,s,t,x,y,w,h)     PROF_SCOPE_AND_STAGE_EXT_COND(cond,p,s,w,h,t)
#else
#define PROFILER_SCOPE_AND_STAGE_EXT2D_(cond,p,s,t,x,y,w,h)      
#endif
#define BX_(cs,ch)  ( ( (cs)->area.block( ComponentID(ch) ).x << getChannelTypeScaleX( ch, (cs)->pcv->chrFormat ) ) >> (cs)->pcv->maxCUSizeLog2 )
#define BY_(cs,ch)  ( ( (cs)->area.block( ComponentID(ch) ).y << getChannelTypeScaleY( ch, (cs)->pcv->chrFormat ) ) >> (cs)->pcv->maxCUSizeLog2 )
#define BW_(cs,ch)  ( Log2( ((cs)->area.block( ComponentID(ch) ).width) ) )
#define BH_(cs,ch)  ( Log2( ((cs)->area.block( ComponentID(ch) ).height) ) )
#define PROFILER_SCOPE_AND_STAGE_EXT2D(cond,p,s,cs,ch)          PROFILER_SCOPE_AND_STAGE_EXT2D_(cond,p,s,!(cs).slice->isIntra(), BX_(cs,ch), BY_(cs,ch), BW_(cs,ch), BH_(cs,ch) )
#endif

}   // namespace vvdec

#endif   // ENABLE_TIME_PROFILING || ENABLE_TIME_PROFILING_EXTENDED

#if ENABLE_TIME_PROFILING
#define PROFILER_ACCUM_AND_START_NEW_SET(cond,p,s)              (*(p))(s)
#define PROFILER_SCOPE_AND_STAGE(cond,p,s)                      PROFILER_SCOPE_AND_STAGE_(cond,p,s)
#define PROFILER_SCOPE_AND_STAGE_EXT(cond,p,s,cs,ch)            PROFILER_SCOPE_AND_STAGE(cond,p,s)
#elif ENABLE_TIME_PROFILING_EXTENDED
#define PROFILER_ACCUM_AND_START_NEW_SET(cond,p,s)              PROFILER_EXT_ACCUM_AND_START_NEW_SET(cond,p,s,0)
#define PROFILER_SCOPE_AND_STAGE(cond,p,s)
#define PROFILER_SCOPE_AND_STAGE_EXT(cond,p,s,cs,ch)            PROFILER_SCOPE_AND_STAGE_EXT2D(cond,p,s,cs,ch)
#else
#define PROF_START(p,s)
#define PROF_STOP(p)
#define PROFILER_ACCUM_AND_START_NEW_SET(cond,p,s)
#define PROFILER_SCOPE_AND_STAGE(cond,p,s)
#define PROFILER_SCOPE_AND_STAGE_EXT(cond,p,s,cs,ch)
#endif
