#include "GCodeTimeEstimator.hpp"
#include <boost/bind.hpp>
#include <cmath>

//###########################################################################################################
#include <fstream>
static const std::string AXIS_STR = "XYZE";
static const float MMMIN_TO_MMSEC = 1.0f / 60.0f;
static const float MILLISEC_TO_SEC = 0.001f;
static const float INCHES_TO_MM = 25.4f;
static const float DEFAULT_FEEDRATE = 0.0f; // <<<<<<<<< FIND A PROPER VALUE
static const float DEFAULT_ACCELERATION = 3000.0f;
static const float DEFAULT_AXIS_MAX_FEEDRATE[] = { 600.0f, 600.0f, 40.0f, 25.0f };
static const float DEFAULT_AXIS_MAX_ACCELERATION[] = { 9000.0f, 9000.0f, 100.0f, 10000.0f };

static const float DEFAULT_AXIS_MAX_JERK[] = { 10.0f, 10.0f, 0.2f, 2.5f }; // from firmware
// static const float DEFAULT_AXIS_MAX_JERK[] = { 20.0f, 20.0f, 0.4f, 5.0f }; / from CURA

static const float MINIMUM_FEEDRATE = 0.01f;
static const float MINIMUM_PLANNER_SPEED = 0.05f; // <<<<<<<< WHAT IS THIS ???
static const float FEEDRATE_THRESHOLD = 0.0001f;
//###########################################################################################################

namespace Slic3r {

//###########################################################################################################
  float My_GCodeTimeEstimator::Block::move_length() const
  {
    float length = ::sqrt(sqr(delta_pos[X]) + sqr(delta_pos[Y]) + sqr(delta_pos[Z]));
    return (length > 0.0f) ? length : ::abs(delta_pos[E]);
  }

  void My_GCodeTimeEstimator::Block::calculate_trapezoid()
  {
    float accelerate_distance = estimate_acceleration_distance(entry_feedrate, feedrate, acceleration);
    float decelerate_distance = estimate_acceleration_distance(feedrate, exit_feedrate, -acceleration);

    float distance = move_length();

    float plateau_distance = distance - accelerate_distance - decelerate_distance;

    // Not enough space to reach the nominal feedrate.
    // This means no cruising, and we'll have to use intersection_distance() to calculate when to abort acceleration 
    // and start braking in order to reach the exit_feedrate exactly at the end of this block.
    if (plateau_distance < 0.0f)
    {
      accelerate_distance = clamp(0.0f, distance, intersection_distance(entry_feedrate, exit_feedrate, acceleration, distance));
      plateau_distance = 0.0f;
    }

    trapezoid.distance = distance;
    trapezoid.accelerate_until = accelerate_distance;
    trapezoid.decelerate_after = accelerate_distance + plateau_distance;
    trapezoid.entry_feedrate = entry_feedrate;
    trapezoid.exit_feedrate = exit_feedrate;
  }

  float My_GCodeTimeEstimator::Block::max_allowable_speed(float acceleration, float target_velocity, float distance)
  {
    return ::sqrt(sqr(target_velocity) - 2.0f * acceleration * distance);
  }

  float My_GCodeTimeEstimator::Block::estimate_acceleration_distance(float initial_rate, float target_rate, float acceleration)
  {
    return (acceleration == 0.0f) ? 0.0f : (sqr(target_rate) - sqr(initial_rate)) / (2.0f * acceleration);
  }

  float My_GCodeTimeEstimator::Block::intersection_distance(float initial_rate, float final_rate, float acceleration, float distance)
  {
    return (acceleration == 0.0f) ? 0.0f : (2.0f * acceleration * distance - sqr(initial_rate) + sqr(final_rate)) / (4.0f * acceleration);
  }

  float My_GCodeTimeEstimator::Block::acceleration_time_from_distance(float initial_feedrate, float distance, float acceleration)
  {
    float discriminant = sqr(initial_feedrate) + 2.0f * acceleration * distance;

    // If discriminant is negative, we're moving in the wrong direction.
    // Making the discriminant 0 then gives the extremum of the parabola instead of the intersection.
    discriminant = std::max(0.0f, discriminant);
    return (-initial_feedrate + ::sqrt(discriminant)) / acceleration;
  }

  My_GCodeTimeEstimator::My_GCodeTimeEstimator()
  {
  }

  void My_GCodeTimeEstimator::parse(const std::string& gcode)
  {
    _reset();
    GCodeReader::parse(gcode, boost::bind(&My_GCodeTimeEstimator::_process_gcode_line, this, _1, _2));
  }

  void My_GCodeTimeEstimator::parse_file(const std::string& file)
  {
    _reset();
    GCodeReader::parse_file(file, boost::bind(&My_GCodeTimeEstimator::_process_gcode_line, this, _1, _2));
  }

  void My_GCodeTimeEstimator::calculate_time()
  {
    _time = get_additional_time();

    for (const Block& block : _blocks)
    {
      const Block::Trapezoid& trapezoid = block.trapezoid;
      float plateau_distance = trapezoid.decelerate_after - trapezoid.accelerate_until;

      _time += Block::acceleration_time_from_distance(block.entry_feedrate, trapezoid.accelerate_until, block.acceleration);
      _time += plateau_distance / block.feedrate;
      _time += Block::acceleration_time_from_distance(block.exit_feedrate, (trapezoid.distance - trapezoid.decelerate_after), block.acceleration);
    }
  }

  void My_GCodeTimeEstimator::set_axis_position(EAxis axis, float position)
  {
    _state.axis[axis].position = position;
  }

  void My_GCodeTimeEstimator::set_axis_max_feedrate(EAxis axis, float feedrate_mm_sec)
  {
    _state.axis[axis].max_feedrate = feedrate_mm_sec;
  }

  void My_GCodeTimeEstimator::set_axis_max_acceleration(EAxis axis, float acceleration)
  {
    _state.axis[axis].max_acceleration = acceleration;
  }

  void My_GCodeTimeEstimator::set_axis_max_jerk(EAxis axis, float jerk)
  {
    _state.axis[axis].max_jerk = jerk;
  }

  float My_GCodeTimeEstimator::get_axis_position(EAxis axis) const
  {
    return _state.axis[axis].position;
  }

  float My_GCodeTimeEstimator::get_axis_max_feedrate(EAxis axis) const
  {
    return _state.axis[axis].max_feedrate;
  }

  float My_GCodeTimeEstimator::get_axis_max_acceleration(EAxis axis) const
  {
    return _state.axis[axis].max_acceleration;
  }

  float My_GCodeTimeEstimator::get_axis_max_jerk(EAxis axis) const
  {
    return _state.axis[axis].max_jerk;
  }

  void My_GCodeTimeEstimator::set_feedrate(float feedrate_mm_sec)
  {
    _state.feedrate = std::max(feedrate_mm_sec, MINIMUM_FEEDRATE);
  }

  float My_GCodeTimeEstimator::get_feedrate() const
  {
    return _state.feedrate;
  }

  void My_GCodeTimeEstimator::set_acceleration(float acceleration)
  {
    _state.acceleration = acceleration;
  }

  float My_GCodeTimeEstimator::get_acceleration() const
  {
    return _state.acceleration;
  }

  void My_GCodeTimeEstimator::set_dialect(My_GCodeTimeEstimator::EDialect dialect)
  {
    _state.dialect = dialect;
  }

  My_GCodeTimeEstimator::EDialect My_GCodeTimeEstimator::get_dialect() const
  {
    return _state.dialect;
  }

  void My_GCodeTimeEstimator::set_units(My_GCodeTimeEstimator::EUnits units)
  {
    _state.units = units;
  }

  My_GCodeTimeEstimator::EUnits My_GCodeTimeEstimator::get_units() const
  {
    return _state.units;
  }

  void My_GCodeTimeEstimator::set_positioningType(My_GCodeTimeEstimator::EPositioningType type)
  {
    _state.positioningType = type;
  }

  My_GCodeTimeEstimator::EPositioningType My_GCodeTimeEstimator::get_positioningType() const
  {
    return _state.positioningType;
  }

  void My_GCodeTimeEstimator::add_additional_time(float timeSec)
  {
    _state.additional_time += timeSec;
  }

  float My_GCodeTimeEstimator::get_additional_time() const
  {
    return _state.additional_time;
  }

  void My_GCodeTimeEstimator::set_default()
  {
    set_units(Millimeters);
    set_dialect(Unknown);
    set_positioningType(Absolute);

    set_feedrate(DEFAULT_FEEDRATE);
    set_acceleration(DEFAULT_ACCELERATION);

    for (unsigned char a = X; a < Num_Axis; ++a)
    {
      EAxis axis = (EAxis)a;
      set_axis_max_feedrate(axis, DEFAULT_AXIS_MAX_FEEDRATE[a]);
      set_axis_max_acceleration(axis, DEFAULT_AXIS_MAX_ACCELERATION[a]);
      set_axis_max_jerk(axis, DEFAULT_AXIS_MAX_JERK[a]);
    }
  }

  float My_GCodeTimeEstimator::get_time() const
  {
    return _time;
  }

  const My_GCodeTimeEstimator::BlocksList& My_GCodeTimeEstimator::get_blocks() const
  {
    return _blocks;
  }

//  void My_GCodeTimeEstimator::print_counters() const
//  {
//    std::cout << std::endl;
//    for (const CmdToCounterMap::value_type& counter : _cmdCounters)
//    {
//      std::cout << counter.first << " : " << counter.second << std::endl;
//    }
//  }

  void My_GCodeTimeEstimator::_reset()
  {
//    _cmdCounters.clear();

    _blocks.clear();

    set_default();
    set_axis_position(X, 0.0f);
    set_axis_position(Y, 0.0f);
    set_axis_position(Z, 0.0f);

    _state.additional_time = 0.0f;
  }

  void My_GCodeTimeEstimator::_process_gcode_line(GCodeReader&, const GCodeReader::GCodeLine& line)
  {
    if (line.cmd.length() > 1)
    {
      switch (line.cmd[0])
      {
      case 'G':
        {
          switch (::atoi(&line.cmd[1]))
          {
          case 1: // Move
            {
              _processG1(line);
              break;
            }
          case 4: // Dwell
            {
              _processG4(line);
              break;
            }
          case 20: // Set Units to Inches
            {
              _processG20(line);
              break;
            }
          case 21: // Set Units to Millimeters
            {
              _processG21(line);
              break;
            }
          case 28: // Move to Origin (Home)
            {
              _processG28(line);
              break;
            }
          case 90: // Set to Absolute Positioning
            {
              _processG90(line);
              break;
            }
          case 91: // Set to Relative Positioning
            {
              _processG91(line);
              break;
            }
          case 92: // Set Position
            {
              _processG92(line);
              break;
            }
          }

          break;
        }
      case 'M':
        {
          switch (::atoi(&line.cmd[1]))
          {
          case 109: // Set Extruder Temperature and Wait
            {
              _processM109(line);
              break;
            }
          case 203: // Set maximum feedrate
            {
              _processM203(line);
              break;
            }
          case 204: // Set default acceleration
            {
              _processM204(line);
              break;
            }
          case 566: // Set allowable instantaneous speed change
            {
              _processM566(line);
              break;
            }
          }

          break;
        }
      }

//      CmdToCounterMap::iterator it = _cmdCounters.find(line.cmd);
//      if (it == _cmdCounters.end())
//        _cmdCounters.insert(CmdToCounterMap::value_type(line.cmd, 1));
//      else
//        ++it->second;
    }
  }

  void My_GCodeTimeEstimator::_processG1(const GCodeReader::GCodeLine& line)
  {
    float lengthsScaleFactor = (get_units() == Inches) ? INCHES_TO_MM : 1.0f;

    // gets position changes from line, if present
    float new_pos[Num_Axis];

    if (get_positioningType() == Absolute)
    {
      for (unsigned char a = X; a < Num_Axis; ++a)
      {
        new_pos[a] = line.has(AXIS_STR[a]) ? line.get_float(AXIS_STR[a]) * lengthsScaleFactor : get_axis_position((EAxis)a);
      }
    }
    else // get_positioningType() == Relative
    {
      for (unsigned char a = X; a < Num_Axis; ++a)
      {
        new_pos[a] = get_axis_position((EAxis)a);
        new_pos[a] += (line.has(AXIS_STR[a]) ? line.get_float(AXIS_STR[a]) * lengthsScaleFactor : 0.0f);
      }
    }

    // updates feedrate from line, if present
    if (line.has('F'))
      set_feedrate(line.get_float('F') * MMMIN_TO_MMSEC);

    // fills block data
    Block block;

    // calculates block movement deltas
    float max_abs_delta = 0.0f;
    for (unsigned char a = X; a < Num_Axis; ++a)
    {
      block.delta_pos[a] = new_pos[a] - get_axis_position((EAxis)a);
      max_abs_delta = std::max(max_abs_delta, ::abs(block.delta_pos[a]));
    }

    // is it a move ?
    if (max_abs_delta == 0.0f)
      return;

    // calculates block feedrate
    float feedrate = get_feedrate();

    float distance = block.move_length();
    float invDistance = 1.0f / distance;

    float axis_feedrate[Num_Axis];
    float min_feedrate_factor = 1.0f;
    for (unsigned char a = X; a < Num_Axis; ++a)
    {
      axis_feedrate[a] = feedrate * ::abs(block.delta_pos[a]) * invDistance;
      if (axis_feedrate[a] > 0.0f)
        min_feedrate_factor = std::min(min_feedrate_factor, get_axis_max_feedrate((EAxis)a) / axis_feedrate[a]);
    }
    
    block.feedrate = min_feedrate_factor * feedrate;
    for (unsigned char a = X; a < Num_Axis; ++a)
    {
      axis_feedrate[a] *= min_feedrate_factor;
    }

    // calculates block acceleration
    float acceleration = get_acceleration();

    for (unsigned char a = X; a < Num_Axis; ++a)
    {
      float axis_max_acceleration = get_axis_max_acceleration((EAxis)a);
      if (acceleration * ::abs(block.delta_pos[a]) * invDistance > axis_max_acceleration)
        acceleration = axis_max_acceleration;
    }

    block.acceleration = acceleration;

    // calculates block exit feedrate
    float exit_feedrate = block.feedrate;

    for (unsigned char a = X; a < Num_Axis; ++a)
    {
      float half_axis_max_jerk = 0.5f * get_axis_max_jerk((EAxis)a);
      if (axis_feedrate[a] > half_axis_max_jerk)
        exit_feedrate = std::min(exit_feedrate, half_axis_max_jerk);
    }

    block.exit_feedrate = exit_feedrate;

    // calculates block entry feedrate
    float vmax_junction = exit_feedrate;
    if (!_blocks.empty() && (_prev.feedrate > FEEDRATE_THRESHOLD))
    {
      vmax_junction = block.feedrate;
      float vmax_junction_factor = 1.0f;

      for (unsigned char a = X; a < Num_Axis; ++a)
      {
        float abs_delta_axis_feedrate = ::abs(axis_feedrate[a] - _prev.axis_feedrate[a]);
        float axis_max_jerk = get_axis_max_jerk((EAxis)a);
        if (abs_delta_axis_feedrate > axis_max_jerk)
          vmax_junction_factor = std::min(vmax_junction_factor, axis_max_jerk / abs_delta_axis_feedrate);
      }

      // limit vmax to not exceed previous feedrate
      vmax_junction = std::min(_prev.feedrate, vmax_junction * vmax_junction_factor);
    }

    block.entry_feedrate = std::min(vmax_junction, Block::max_allowable_speed(-acceleration, MINIMUM_PLANNER_SPEED, distance));

    // calculates block trapezoid
    block.calculate_trapezoid();

    // updates previous cache
    _prev.feedrate = feedrate;
    for (unsigned char a = X; a < Num_Axis; ++a)
    {
      _prev.axis_feedrate[a] = axis_feedrate[a];
    }

    // updates axis positions
    for (unsigned char a = X; a < Num_Axis; ++a)
    {
      set_axis_position((EAxis)a, new_pos[a]);
    }

    // adds block to blocks list
    _blocks.push_back(block);
  }

  void My_GCodeTimeEstimator::_processG4(const GCodeReader::GCodeLine& line)
  {
    EDialect dialect = get_dialect();

    if (line.has('P'))
      add_additional_time(line.get_float('P') * MILLISEC_TO_SEC);

    // see: http://reprap.org/wiki/G-code#G4:_Dwell
    if ((dialect == Repetier) ||
        (dialect == Marlin) ||
        (dialect == Smoothieware) ||
        (dialect == RepRapFirmware))
    {
      if (line.has('S'))
        add_additional_time(line.get_float('S'));
    }
  }

  void My_GCodeTimeEstimator::_processG20(const GCodeReader::GCodeLine& line)
  {
    set_units(Inches);
  }

  void My_GCodeTimeEstimator::_processG21(const GCodeReader::GCodeLine& line)
  {
    set_units(Millimeters);
  }

  void My_GCodeTimeEstimator::_processG28(const GCodeReader::GCodeLine& line)
  {
    // todo
  }

  void My_GCodeTimeEstimator::_processG90(const GCodeReader::GCodeLine& line)
  {
    set_positioningType(Absolute);
  }

  void My_GCodeTimeEstimator::_processG91(const GCodeReader::GCodeLine& line)
  {
    // >>>>>>>> THERE ARE DIALECT VARIANTS

    set_positioningType(Relative);
  }

  void My_GCodeTimeEstimator::_processG92(const GCodeReader::GCodeLine& line)
  {
    // todo
  }

  void My_GCodeTimeEstimator::_processM109(const GCodeReader::GCodeLine& line)
  {
    // todo
  }

  void My_GCodeTimeEstimator::_processM203(const GCodeReader::GCodeLine& line)
  {
    EDialect dialect = get_dialect();

    // see http://reprap.org/wiki/G-code#M203:_Set_maximum_feedrate
    if (dialect == Repetier)
      return;

    // see http://reprap.org/wiki/G-code#M203:_Set_maximum_feedrate
    float factor = (dialect == Marlin) ? 1.0f : MMMIN_TO_MMSEC;

    if (line.has('X'))
      set_axis_max_feedrate(X, line.get_float('X') * factor);

    if (line.has('Y'))
      set_axis_max_feedrate(Y, line.get_float('Y') * factor);

    if (line.has('Z'))
      set_axis_max_feedrate(Z, line.get_float('Z') * factor);

    if (line.has('E'))
      set_axis_max_feedrate(E, line.get_float('E') * factor);
  }

  void My_GCodeTimeEstimator::_processM204(const GCodeReader::GCodeLine& line)
  {
    if (line.has('S'))
      set_acceleration(line.get_float('S')); // <<<< Is this correct ?

    if (line.has('T'))
    {
      // what to do ?
    }
  }

  void My_GCodeTimeEstimator::_processM566(const GCodeReader::GCodeLine& line)
  {
    if (line.has('X'))
      set_axis_max_jerk(X, line.get_float('X') * MMMIN_TO_MMSEC);

    if (line.has('Y'))
      set_axis_max_jerk(Y, line.get_float('Y') * MMMIN_TO_MMSEC);

    if (line.has('Z'))
      set_axis_max_jerk(Z, line.get_float('Z') * MMMIN_TO_MMSEC);

    if (line.has('E'))
      set_axis_max_jerk(E, line.get_float('E') * MMMIN_TO_MMSEC);
  }
//###########################################################################################################

void
GCodeTimeEstimator::parse(const std::string &gcode)
{
    GCodeReader::parse(gcode, boost::bind(&GCodeTimeEstimator::_parser, this, _1, _2));
}

void
GCodeTimeEstimator::parse_file(const std::string &file)
{
    GCodeReader::parse_file(file, boost::bind(&GCodeTimeEstimator::_parser, this, _1, _2));
}

void
GCodeTimeEstimator::_parser(GCodeReader&, const GCodeReader::GCodeLine &line)
{
    // std::cout << "[" << this->time << "] " << line.raw << std::endl;
    if (line.cmd == "G1") {
        const float dist_XY = line.dist_XY();
        const float new_F = line.new_F();
        
        if (dist_XY > 0) {
            //this->time += dist_XY / new_F * 60;
            this->time += _accelerated_move(dist_XY, new_F/60, this->acceleration);
        } else {
            //this->time += std::abs(line.dist_E()) / new_F * 60;
            this->time += _accelerated_move(std::abs(line.dist_E()), new_F/60, this->acceleration);
        }
        //this->time += std::abs(line.dist_Z()) / new_F * 60;
        this->time += _accelerated_move(std::abs(line.dist_Z()), new_F/60, this->acceleration);
    } else if (line.cmd == "M204" && line.has('S')) {
        this->acceleration = line.get_float('S');
    } else if (line.cmd == "G4") { // swell
        if (line.has('S')) {
            this->time += line.get_float('S');
        } else if (line.has('P')) {
            this->time += line.get_float('P')/1000;
        }
    }
}

// Wildly optimistic acceleration "bell" curve modeling.
// Returns an estimate of how long the move with a given accel
// takes in seconds.
// It is assumed that the movement is smooth and uniform.
float
GCodeTimeEstimator::_accelerated_move(double length, double v, double acceleration) 
{
    // for half of the move, there are 2 zones, where the speed is increasing/decreasing and 
    // where the speed is constant.
    // Since the slowdown is assumed to be uniform, calculate the average velocity for half of the 
    // expected displacement.
    // final velocity v = a*t => a * (dx / 0.5v) => v^2 = 2*a*dx
    // v_avg = 0.5v => 2*v_avg = v
    // d_x = v_avg*t => t = d_x / v_avg
    acceleration = (acceleration == 0.0 ? 4000.0 : acceleration); // Set a default accel to use for print time in case it's 0 somehow.
    auto half_length = length / 2.0;
    auto t_init = v / acceleration; // time to final velocity
    auto dx_init = (0.5*v*t_init); // Initial displacement for the time to get to final velocity
    auto t = 0.0;
    if (half_length >= dx_init) {
        half_length -= (0.5*v*t_init);
        t += t_init;
        t += (half_length / v); // rest of time is at constant speed.
    } else {
        // If too much displacement for the expected final velocity, we don't hit the max, so reduce 
        // the average velocity to fit the displacement we actually are looking for.
        t += std::sqrt(std::abs(length) * 2.0 * acceleration) / acceleration;
    }
    return 2.0*t; // cut in half before, so double to get full time spent.
}

}
