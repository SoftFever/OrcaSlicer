#ifndef slic3r_GCodeTimeEstimator_hpp_
#define slic3r_GCodeTimeEstimator_hpp_

#include "libslic3r.h"
#include "GCodeReader.hpp"

namespace Slic3r {

//###########################################################################################################
  class My_GCodeTimeEstimator : public GCodeReader
  {
  public:
    enum EUnits : unsigned char
    {
      Millimeters,
      Inches
    };

    enum EAxis : unsigned char
    {
      X,
      Y,
      Z,
      E,
      Num_Axis
    };

    enum EDialect : unsigned char
    {
      Unknown,
      Marlin,
      Repetier,
      Smoothieware,
      RepRapFirmware,
      Teacup,
      Num_Dialects
    };

    enum EPositioningType
    {
      Absolute,
      Relative
    };

  private:
    struct Axis
    {
      float position;         // mm
      float max_feedrate;     // mm/s
      float max_acceleration; // mm/s^2
      float max_jerk;         // mm/s
    };

    struct State
    {
      EDialect dialect;
      EUnits units;
      EPositioningType positioningType;
      Axis axis[Num_Axis];
      float feedrate;                     // mm/s
      float acceleration;                 // mm/s^2
      float additional_time;              // s
    };

    struct PreviousBlockCache
    {
      float feedrate;                // mm/s
      float axis_feedrate[Num_Axis]; // mm/s
    };

  public:
    struct Block
    {
      struct Trapezoid
      {
        float distance;         // mm
        float accelerate_until; // mm
        float decelerate_after; // mm
        float entry_feedrate;   // mm/s
        float exit_feedrate;    // mm/s
      };

      float delta_pos[Num_Axis]; // mm
      float feedrate;            // mm/s
      float acceleration;        // mm/s^2
      float entry_feedrate;      // mm/s
      float exit_feedrate;       // mm/s

      Trapezoid trapezoid;

      // Returns the length of the move covered by this block, in mm
      float move_length() const;

      void calculate_trapezoid();

      // Calculates the maximum allowable speed at this point when you must be able to reach target_velocity using the 
      // acceleration within the allotted distance.
      static float max_allowable_speed(float acceleration, float target_velocity, float distance);

      // Calculates the distance (not time) it takes to accelerate from initial_rate to target_rate using the given acceleration:
      static float estimate_acceleration_distance(float initial_rate, float target_rate, float acceleration);

      // This function gives you the point at which you must start braking (at the rate of -acceleration) if 
      // you started at speed initial_rate and accelerated until this point and want to end at the final_rate after
      // a total travel of distance. This can be used to compute the intersection point between acceleration and
      // deceleration in the cases where the trapezoid has no plateau (i.e. never reaches maximum speed)
      static float intersection_distance(float initial_rate, float final_rate, float acceleration, float distance);

      // This function gives the time it needs to accelerate from an initial speed to reach a final distance.
      static float acceleration_time_from_distance(float initial_feedrate, float distance, float acceleration);
    };

    typedef std::vector<Block> BlocksList;

  private:
//    typedef std::map<std::string, unsigned int> CmdToCounterMap;
//    CmdToCounterMap _cmdCounters;

    State _state;
    PreviousBlockCache _prev;
    BlocksList _blocks;
    float _time; // s

  public:
    My_GCodeTimeEstimator();

    void parse(const std::string& gcode);
    void parse_file(const std::string& file);

    void calculate_time();

    void set_axis_position(EAxis axis, float position);
    void set_axis_max_feedrate(EAxis axis, float feedrate_mm_sec);
    void set_axis_max_acceleration(EAxis axis, float acceleration);
    void set_axis_max_jerk(EAxis axis, float jerk);

    float get_axis_position(EAxis axis) const;
    float get_axis_max_feedrate(EAxis axis) const;
    float get_axis_max_acceleration(EAxis axis) const;
    float get_axis_max_jerk(EAxis axis) const;

    void set_feedrate(float feedrate_mm_sec);
    float get_feedrate() const;

    void set_acceleration(float acceleration);
    float get_acceleration() const;

    void set_dialect(EDialect dialect);
    EDialect get_dialect() const;

    void set_units(EUnits units);
    EUnits get_units() const;

    void set_positioningType(EPositioningType type);
    EPositioningType get_positioningType() const;

    void add_additional_time(float timeSec);
    float get_additional_time() const;

    void set_default();

    // returns estimated time in seconds
    float get_time() const;

    const BlocksList& get_blocks() const;

//    void print_counters() const;

  private:
    void _reset();

    // Processes GCode line
    void _process_gcode_line(GCodeReader&, const GCodeReader::GCodeLine& line);

    // Move
    void _processG1(const GCodeReader::GCodeLine& line);

    // Dwell
    void _processG4(const GCodeReader::GCodeLine& line);

    // Set Units to Inches
    void _processG20(const GCodeReader::GCodeLine& line);

    // Set Units to Millimeters
    void _processG21(const GCodeReader::GCodeLine& line);

    // Move to Origin (Home)
    void _processG28(const GCodeReader::GCodeLine& line);

    // Set to Absolute Positioning
    void _processG90(const GCodeReader::GCodeLine& line);

    // Set to Relative Positioning
    void _processG91(const GCodeReader::GCodeLine& line);

    // Set Position
    void _processG92(const GCodeReader::GCodeLine& line);

    // Set Extruder Temperature and Wait
    void _processM109(const GCodeReader::GCodeLine& line);

    // Set maximum feedrate
    void _processM203(const GCodeReader::GCodeLine& line);

    // Set default acceleration
    void _processM204(const GCodeReader::GCodeLine& line);

    // Set allowable instantaneous speed change
    void _processM566(const GCodeReader::GCodeLine& line);
  };
//###########################################################################################################

class GCodeTimeEstimator : public GCodeReader {
    public:
    float time = 0;  // in seconds
    
    void parse(const std::string &gcode);
    void parse_file(const std::string &file);
    
    protected:
    float acceleration = 9000;
    void _parser(GCodeReader&, const GCodeReader::GCodeLine &line);
    static float _accelerated_move(double length, double v, double acceleration);
};

} /* namespace Slic3r */

#endif /* slic3r_GCodeTimeEstimator_hpp_ */
