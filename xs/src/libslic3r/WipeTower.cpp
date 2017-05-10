#include "WipeTower.hpp"

#include <assert.h>
#include <fstream>
#include <iostream>

#ifdef __linux
#include <strings.h>
#endif /* __linux */

#ifdef _MSC_VER 
#define strcasecmp _stricmp
#endif

namespace PrusaSingleExtruderMM
{

static inline std::string line_XY(float x, float y)
{
	char buf[128];
	sprintf(buf, "G1 X%.3f Y%.3f\n", x, y);
	return buf;
}

static inline std::string line_XYE(float x, float y, float e)
{
	char buf[128];
	sprintf(buf, "G1 X%.3f Y%.3f E%.4f\n", x, y, e);
	return buf;
}

static inline std::string line_XYF(float x, float y, float f)
{
	char buf[128];
	sprintf(buf, "G1 X%.3f Y%.3f F%.0f\n", x, y, f);
	return buf;
}

static inline std::string line_ZF(float z, float f)
{
	char buf[128];
	sprintf(buf, "G1 Z%.3f F%.0f\n", z, f);
	return buf;
}

static inline std::string line_F(float f)
{
	char buf[128];
	sprintf(buf, "G1 F%.0f\n", f);
	return buf;
}

static inline std::string line_XEF(float x, float e, float f)
{
	char buf[128];
	sprintf(buf, "G1 X%.3f E%.4f F%.0f\n", x, e, f);
	return buf;
}

static inline std::string line_EF(float e, float f)
{
	char buf[128];
	sprintf(buf, "G1 E%.4f F%.0f\n", e, f);
	return buf;
}

// Set extruder temperature, don't wait.
static inline std::string line_M104(int temperature)
{
	char buf[128];
	sprintf(buf, "M104 S%d\n", temperature);
	return buf;
};

// Set extruder temperature and wait.
static inline std::string line_M109(int temperature)
{
	char buf[128];
	sprintf(buf, "M109 S%d\n", temperature);
	buf;
};

// Set maximum feedrate
static inline std::string line_M203(int feedrate)
{
	char buf[128];
	sprintf(buf, "M203 E%d\n", feedrate);
	return buf;
};

// Set speed factor override percentage
static inline std::string line_M220(int speed)
{
	char buf[128];
	sprintf(buf, "M220 S%d\n", speed);
	return buf;
};

// Set digital trimpot motor
static inline std::string line_M907(int current)
{
	char buf[128];
	sprintf(buf, "M907 E%d\n", current);
	return buf;
};

// Dwell for seconds. If delay == 0, this just flushes planner queue.
static inline std::string line_G4(int delay)
{
	char buf[128];
	sprintf(buf, "G4 S%d\n", delay);
	return buf;
};

// Reset internal extruder counter.
static inline std::string line_ResetE()
{
	return "G92 E0.0\n";
};

static inline std::string line_CommentValue(const char *comment, int value)
{
	char strvalue[15];
	sprintf(strvalue, "%d", value);
	return std::string(";") + comment + strvalue + "\n";
};

static inline std::string line_CommentMaterial(WipeTower::material_type material)
{
	std::string ret("; material : ");

	switch (material)
	{
	case WipeTower::PVA:
		ret += "#8 (PVA)";
		break;
	case WipeTower::SCAFF:
		ret += "#5 (Scaffold)";
		break;
	case WipeTower::FLEX:
		ret += "#4 (Flex)";
		break;
	default:
		ret += "DEFAULT (PLA)";
		break;
	}
	
	return ret + "\n";
};

static inline int randi(int lo, int hi)
{
	int n = hi - lo + 1;
	int i = rand() % n;
	if (i < 0) i = -i;
	return lo + i;
}

WipeTower::material_type WipeTower::parse_material(const char *name)
{
	if (strcasecmp(name, "PLA") == 0)
		return PLA;
	if (strcasecmp(name, "ABS") == 0)
		return ABS;
	if (strcasecmp(name, "PET") == 0)
		return PET;
	if (strcasecmp(name, "HIPS") == 0)
		return HIPS;
	if (strcasecmp(name, "FLEX") == 0)
		return FLEX;
	if (strcasecmp(name, "SCAFF") == 0)
		return SCAFF;
	if (strcasecmp(name, "EDGE") == 0)
		return EDGE;
	if (strcasecmp(name, "NGEN") == 0)
		return NGEN;
	if (strcasecmp(name, "PVA") == 0)
		return PVA;
	return INVALID;
}

std::string WipeTower::FirstLayer(bool sideOnly, float offset)
{
	float _ext = extrusion_flow + ((extrusion_flow / 100) * 10);
	
	box_coordinates wipeTower_box(m_wipe_tower_pos, m_wipe_tower_width, m_wipe_area * float(m_color_changes) - perimeterWidth / 2);
	
	std::string gcode = 
			";-------------------------------------\n"
			"; CP WIPE TOWER FIRST LAYER BRIM START\n";

	gcode += line_ZF(m_z_pos + zHop, 7200);

	gcode += line_F(6000);
	gcode += line_XY( wipeTower_box.lu.x - (perimeterWidth * 10), wipeTower_box.lu.y );
	gcode += line_ZF(m_z_pos, 7200);
	
	gcode += line_F(2400);
	gcode += line_XYE(wipeTower_box.ld.x - (perimeterWidth * 10), wipeTower_box.ld.y, retract);

	float _offset = 0; 
	gcode += line_F(2100);
	int _per = 0;
	
	if (sideOnly)
	{
		do
		{
			_offset = _offset + perimeterWidth;
			gcode += line_XY(wipeTower_box.ld.x - _offset, wipeTower_box.ld.y + offset);
			gcode += line_XYE(wipeTower_box.lu.x - _offset, wipeTower_box.lu.y - offset, (wipeTower_box.lu.y - wipeTower_box.ld.y) * _ext);
			_per++;
		} while (_per < 4);
		gcode += line_F(7000);
		gcode += line_XY(wipeTower_box.rd.x + _offset, wipeTower_box.ld.y + offset);
		gcode += line_F(2100);
		_per = 0;
		_offset = 0;
		do
		{
			_offset = _offset + perimeterWidth;
			gcode += line_XY(wipeTower_box.rd.x + _offset, wipeTower_box.ld.y + offset);
			gcode += line_XYE(wipeTower_box.ru.x + _offset, wipeTower_box.lu.y - offset, (wipeTower_box.lu.y - wipeTower_box.ld.y) * _ext);
			_per++;
		} while (_per < 4);

	}
	else
	{
		do
		{
			_offset = _offset + perimeterWidth;
			float _ext_X = ((wipeTower_box.rd.x + _offset) - (wipeTower_box.ld.x - _offset))* _ext;
			float _ext_Y = ((wipeTower_box.lu.y + _offset) - (wipeTower_box.ld.y - _offset))* _ext;

			float __x0 = wipeTower_box.ld.x - _offset + (perimeterWidth / 2);
			float __y0 = wipeTower_box.ld.y - _offset + perimeterWidth;
			gcode += line_XY(__x0, __y0);

			float __x1 = wipeTower_box.lu.x - _offset + (perimeterWidth / 2);
			float __y1 = wipeTower_box.lu.y + _offset;
			gcode += line_XYE(__x1, __y1, _ext_Y);

			float __x2 = wipeTower_box.ru.x + _offset - (perimeterWidth / 2);
			float __y2 = wipeTower_box.ru.y + _offset;
			gcode += line_XYE(__x2, __y2, _ext_X);

			float __x3 = wipeTower_box.rd.x + _offset - (perimeterWidth / 2);
			float __y3 = wipeTower_box.rd.y - _offset + perimeterWidth;
			gcode += line_XYE(__x3, __y3, _ext_Y);

			float __x4 = wipeTower_box.ld.x - _offset + (perimeterWidth / 2);
			float __y4 = wipeTower_box.ld.y - _offset + perimeterWidth;
			gcode += line_XYE(__x4, __y4, _ext_X);
			_per++;

		} while (_per < 4);


	}

	gcode += line_F(7000);
	gcode += line_XY(wipeTower_box.ld.x, wipeTower_box.ld.y);
	gcode += line_XY(wipeTower_box.rd.x, wipeTower_box.ld.y);
	gcode += line_XY(wipeTower_box.ld.x, wipeTower_box.ld.y);

	gcode += "; CP WIPE TOWER FIRST LAYER BRIM END\n";
	gcode += ";-----------------------------------\n";
	return gcode;
}

std::pair<std::string, WipeTower::xy> WipeTower::Toolchange(int tool, material_type current_material, material_type new_material, int temperature, wipe_shape shape, int count, float spaceAvailable, float wipeStartY, bool lastInFile, bool colorInit)
{
	box_coordinates cleaning_box(
		m_wipe_tower_pos.x,
		m_wipe_tower_pos.y + wipeStartY, //(order * _wipe_area); //wipeStartY;
		m_wipe_tower_width, 
		spaceAvailable - perimeterWidth / 2);  //space_available //wipe_area

	std::string gcode;
	gcode += ";--------------------\n";
	gcode += "; CP TOOLCHANGE START\n";
	gcode += line_CommentValue(" toolchange #", count);
	gcode += line_CommentMaterial(current_material);
	gcode += ";--------------------\n";

	gcode += line_M220(100);
	gcode += line_ZF(m_z_pos + zHop, 7200);

	gcode += line_EF((retract/2)*-1, 3600);				// additional retract on move to tower
	gcode += line_XYF(cleaning_box.ld.x + perimeterWidth, cleaning_box.ld.y + shape * perimeterWidth, 7200);
	gcode += line_ZF(m_z_pos, 7200);
	gcode += line_EF((retract / 2), 3600);					// additional retract on move to tower
	gcode += line_EF((retract), 1500);	
	
	m_y_position = (shape == NORMAL) ? cleaning_box.ld.y : cleaning_box.lu.y;
	
	gcode += line_M907(750);
	gcode += line_G4(0);

	gcode += toolchange_Unload(cleaning_box, current_material, shape, temperature);

	if (!lastInFile)
	{
		gcode += toolchange_Change(tool, current_material, new_material);
		gcode += toolchange_Load(cleaning_box, current_material, shape, colorInit);
		gcode += toolchange_Wipe(cleaning_box, current_material, shape);
		gcode += toolchange_Done(cleaning_box, current_material, shape);
	}

	gcode += line_M907(550);
	gcode += line_G4(0);
	gcode += line_ResetE();
	
	gcode += "; CP TOOLCHANGE END\n"
		     ";------------------\n"
			 "\n\n";
	return std::pair<std::string, xy>(gcode, (shape == NORMAL) ? cleaning_box.lu : cleaning_box.ld);
}

std::string WipeTower::toolchange_Unload(const box_coordinates &cleaning_box, material_type material, wipe_shape shape, int temperature)
{
	float __xl = 0;
	float __xr = 0;
	__xl = cleaning_box.ld.x + (perimeterWidth / 2);
	__xr = cleaning_box.rd.x - (perimeterWidth / 2);

	std::string gcode = "; CP TOOLCHANGE UNLOAD";

	switch (material)
	{
	case PVA:

		gcode += line_F(4000);
		m_y_position += shape * perimeterWidth * 1.2f;
		gcode += line_XYE(__xl + (perimeterWidth * 2), m_y_position, 0);
		gcode += line_XYE(__xr - perimeterWidth, m_y_position, 3);

		gcode += line_F(4500);
		m_y_position += shape * perimeterWidth * 1.5f;
		gcode += line_XYE(__xr - perimeterWidth, m_y_position, 0);
		gcode += line_XYE(__xl + perimeterWidth, m_y_position, 3);

		gcode += line_F(4800);
		m_y_position += shape * perimeterWidth * 1.5f;
		gcode += line_XYE(__xl + (perimeterWidth * 2), m_y_position, 0);
		gcode += line_XYE(__xr - (perimeterWidth * 2), m_y_position, 3);

		gcode += line_F(5000);
		m_y_position += shape * perimeterWidth * 1.5f;
		gcode += line_XYE(__xr - perimeterWidth, m_y_position, 0);
		gcode += line_XYE(__xl + perimeterWidth, m_y_position, 3);

		gcode += line_EF(-15, 5000);
		gcode += line_EF(-50, 5400);
		gcode += line_EF(-15, 3000);
		gcode += line_EF(-12, 2000);


		if (temperature != 0)
		{
			gcode += line_M104(temperature);
		}

		// cooling moves
		m_y_position += shape * perimeterWidth * 0.8f;

		gcode += line_F(1600);
		gcode += line_XYE(__xl, m_y_position, 3);
		gcode += line_XYE(__xr, m_y_position, -5);

		gcode += line_F(2000);
		gcode += line_XYE(__xl, m_y_position, 5);
		gcode += line_XYE(__xr, m_y_position, -5);

		gcode += line_F(2200);
		gcode += line_XYE(__xl, m_y_position, 5);
		gcode += line_XYE(__xr, m_y_position, -5);

		gcode += line_F(2400);
		gcode += line_XYE(__xl, m_y_position, 5);
		gcode += line_XYE(__xr, m_y_position, -5);

		gcode += line_F(2400);
		gcode += line_XYE(__xl, m_y_position, 5);
		gcode += line_XYE(__xr, m_y_position, -5);

		gcode += line_F(2400);
		gcode += line_XYE(__xl, m_y_position, 5);
		gcode += line_XYE(__xr, m_y_position, -3);

		gcode += line_G4(0);

		break;  // end of SCAFF


	case SCAFF:

		gcode += line_F(4000);
		m_y_position += shape * perimeterWidth * 3.f;
		gcode += line_XYE(__xl + (perimeterWidth * 2), m_y_position, 0);
		gcode += line_XYE(__xr - perimeterWidth, m_y_position, 3);

		gcode += line_F(4600);
		m_y_position += shape * perimeterWidth * 3.f;
		gcode += line_XYE(__xr - perimeterWidth, m_y_position, 0);
		gcode += line_XYE(__xl + perimeterWidth, m_y_position, 4);

		gcode += line_F(5200);
		m_y_position += shape * perimeterWidth * 3.f;
		gcode += line_XYE(__xl + (perimeterWidth * 2), m_y_position, 0);
		gcode += line_XYE(__xr - (perimeterWidth * 2), m_y_position, 4.5);

		gcode += line_EF(-15, 5000);
		gcode += line_EF(-50, 5400);
		gcode += line_EF(-15, 3000);
		gcode += line_EF(-12, 2000);


		if (temperature != 0)
		{
			gcode += line_M104(temperature);
		}

		// cooling moves
		m_y_position += shape * perimeterWidth * 0.8f;

		gcode += line_F(1600);
		gcode += line_XYE(__xl, m_y_position, 3);
		gcode += line_XYE(__xr, m_y_position, -5);

		gcode += line_F(2000);
		gcode += line_XYE(__xl, m_y_position, 5);
		gcode += line_XYE(__xr, m_y_position, -5);

		gcode += line_F(2200);
		gcode += line_XYE(__xl, m_y_position, 5);
		gcode += line_XYE(__xr, m_y_position, -5);

		gcode += line_F(2200);
		gcode += line_XYE(__xl, m_y_position, 5);
		gcode += line_XYE(__xr, m_y_position, -5);

		gcode += line_F(2400);
		gcode += line_XYE(__xl, m_y_position, 5);
		gcode += line_XYE(__xr, m_y_position, -3);

		gcode += line_G4(0);

		break;  // end of SCAFF
			

	default:

		gcode += line_F(4000);
		m_y_position += shape * perimeterWidth * 1.2f;
		gcode += line_XYE(__xl + (perimeterWidth * 2), m_y_position, 0);
		gcode += line_XYE(__xr - perimeterWidth, m_y_position, (__xr - __xl) * (extrusion_flow*(float)1.6));

		gcode += line_F(4600);
		m_y_position += shape * perimeterWidth * 1.2f;
		gcode += line_XYE(__xr - perimeterWidth, m_y_position, perimeterWidth*extrusion_flow);
		gcode += line_XYE(__xl + perimeterWidth, m_y_position, (__xr - __xl) * (extrusion_flow*(float)1.65));

		gcode += line_F(5200);
		m_y_position += shape * perimeterWidth * 1.2f; //1.4f
		gcode += line_XYE(__xl + (perimeterWidth * 2), m_y_position, perimeterWidth*extrusion_flow);
		gcode += line_XYE(__xr - (perimeterWidth * 2), m_y_position, (__xr - __xl) * (extrusion_flow*(float)1.74));

		gcode += line_EF(-15, 5000);
		gcode += line_EF(-50, 5400);
		gcode += line_EF(-15, 3000);
		gcode += line_EF(-12, 2000);


		if (temperature != 0)
		{
			gcode += line_M104(temperature);
		}

		// cooling moves
		m_y_position += shape * perimeterWidth * 0.8f;

		gcode += line_F(1600);
		gcode += line_XYE(__xl, m_y_position, 3);
		gcode += line_XYE(__xr, m_y_position, -5);

		gcode += line_F(2000);
		gcode += line_XYE(__xl, m_y_position, 5);
		gcode += line_XYE(__xr, m_y_position, -5);

		gcode += line_F(2400);
		gcode += line_XYE(__xl, m_y_position, 5);
		gcode += line_XYE(__xr, m_y_position, -5);

		gcode += line_F(2400);
		gcode += line_XYE(__xl, m_y_position, 5);
		gcode += line_XYE(__xr, m_y_position, -3);

		gcode += line_G4(0);
		
		break;  // end of default material
	}

	return gcode;
}

std::string WipeTower::toolchange_Load(const box_coordinates &cleaning_box, material_type material, wipe_shape shape, bool colorInit)
{
	float __xl = 0;
	float __xr = 0;

	std::string gcode = "; CP TOOLCHANGE LOAD\n";

	switch (material)
	{
	default:

		__xl = cleaning_box.ld.x + (perimeterWidth * 1);
		__xr = cleaning_box.rd.x - (perimeterWidth * 1);
		float _extrusion = (__xr - __xl) * extrusion_flow;

		gcode += line_XEF(__xr, 20, 1400);
		gcode += line_XEF(__xl, 40, 3000);
		gcode += line_XEF(__xr, 20, 1600);
		gcode += line_XEF(__xl, 10, 1000);
		
		gcode += line_F(1600);
		gcode += line_XYE(__xr, m_y_position, _extrusion);

		gcode += line_F(2200);

		int _pass = 2;
		if (colorInit) _pass = 1;

		for (int _i = 0; _i < _pass; _i++)
		{
			m_y_position += shape * perimeterWidth * 0.85f;
			gcode += line_XY(__xr, m_y_position);
			gcode += line_XYE(__xl, m_y_position, _extrusion);

			m_y_position += shape * perimeterWidth * 0.85f;
			gcode += line_XY(__xl, m_y_position);
			gcode += line_XYE(__xr, m_y_position, _extrusion);
		}

		break;
	}

	gcode += line_M907(550);
	return gcode;
}

std::string WipeTower::toolchange_Wipe(const box_coordinates &cleaning_box, material_type material, wipe_shape shape)
{
	// wipe new filament until end of wipe area
	float __xl = 0;
	float __xr = 0;
	int __p = 0;
	float _wipespeed = 4200;
	float _extr = extrusion_flow;
	float _wipekoef = 1;

	std::string gcode = "; CP TOOLCHANGE WIPE\n";

	// increase flow on first layer, slow down print
	if (int(m_z_pos * 100) < 21)
	{
		_extr = extrusion_flow + ((extrusion_flow / 100) * 18);
		_wipekoef = (float)0.5;
	}
	else
	{
		_extr = extrusion_flow;
		_wipekoef = (float)1;
	}

	switch (material)
	{

	default:

		__xl = cleaning_box.ld.x + (perimeterWidth*2);
		__xr = cleaning_box.rd.x - (perimeterWidth*2);

		switch (shape)
		{
		case NORMAL:
			do
			{
				__p++;
				_wipespeed = _wipespeed + 50;
				if (_wipespeed > 4800) { _wipespeed = 4800; }
				gcode += line_F(_wipespeed * _wipekoef);
				m_y_position += shape * perimeterWidth * 0.7f;
				if (__p < 2)
				{
					gcode += line_XYE(__xl - (perimeterWidth/2), m_y_position, perimeterWidth * _extr);
					gcode += line_XYE(__xr + (perimeterWidth), m_y_position, (__xr - __xl) * _extr);
				}
				else
				{
					gcode += line_XYE(__xl-(perimeterWidth), m_y_position, perimeterWidth * _extr);
					gcode += line_XYE(__xr+(perimeterWidth*2), m_y_position, (__xr - __xl) * _extr);
					__p = 0;
				}
				_wipespeed = _wipespeed + 50;
				if (_wipespeed > 4800) { _wipespeed = 4800; }
				gcode += line_F(_wipespeed * _wipekoef);
				m_y_position += shape * perimeterWidth * 0.7f;
				gcode += line_XYE(__xr + (perimeterWidth), m_y_position, perimeterWidth * _extr);
				gcode += line_XYE(__xl - (perimeterWidth), m_y_position, (__xr - __xl) * _extr);

			} while (m_y_position <= cleaning_box.lu.y - (perimeterWidth*1));
			break;

		case REVERSED:
			do
			{
				__p++;
				_wipespeed = _wipespeed + 50;
				if (_wipespeed > 4900) { _wipespeed = 4900; }
				gcode += line_F(_wipespeed * _wipekoef);
				m_y_position += shape * perimeterWidth * 0.7f;
				if (__p < 2)
				{
					gcode += line_XYE(__xl - (perimeterWidth/2), m_y_position, perimeterWidth * _extr);
					gcode += line_XYE(__xr + (perimeterWidth), m_y_position, (__xr - __xl) * _extr);
				}
				else
				{
					gcode += line_XYE(__xl - (perimeterWidth), m_y_position, perimeterWidth * _extr);
					gcode += line_XYE(__xr + (perimeterWidth *2), m_y_position, (__xr - __xl) * _extr);
					__p = 0;

				}
				_wipespeed = _wipespeed + 50;
				if (_wipespeed > 4900) { _wipespeed = 4900; }
				gcode += line_F(_wipespeed * _wipekoef);
				m_y_position += shape * perimeterWidth * 0.7f;
				gcode += line_XYE(__xr + (perimeterWidth), m_y_position, perimeterWidth * _extr);
				gcode += line_XYE(__xl - (perimeterWidth), m_y_position, (__xr - __xl) * _extr);

			} while (m_y_position >= cleaning_box.ld.y + (perimeterWidth*1 ));
			break;
		}

		break;
	}

	return gcode;
}

std::string WipeTower::toolchange_Done(const box_coordinates &cleaning_box, material_type /* material */, wipe_shape shape)
{
	std::string gcode;

	switch (shape)
	{
	case NORMAL:
		gcode += line_F(7000);
		gcode += line_XY(cleaning_box.lu.x, cleaning_box.lu.y);
		gcode += line_F(3200);
		gcode += line_XYE(cleaning_box.ld.x, cleaning_box.ld.y, (cleaning_box.lu.y - cleaning_box.ld.y)*(extrusion_flow ));
		gcode += line_XYE(cleaning_box.rd.x, cleaning_box.rd.y, (cleaning_box.rd.x - cleaning_box.ld.x)*(extrusion_flow ));
		gcode += line_XYE(cleaning_box.ru.x, cleaning_box.lu.y, (cleaning_box.lu.y - cleaning_box.ld.y)*(extrusion_flow ));
		gcode += line_XYE(cleaning_box.lu.x, cleaning_box.lu.y, (cleaning_box.rd.x - cleaning_box.ld.x)*(extrusion_flow ));

		gcode += line_F(7200);
		gcode += line_XY(cleaning_box.ru.x, cleaning_box.lu.y);
		gcode += line_XY(cleaning_box.lu.x, cleaning_box.lu.y);
		

		gcode += line_F(6000);
		break;

	case REVERSED:
		gcode += line_F(7000);
		gcode += line_XY(cleaning_box.ld.x , cleaning_box.ld.y );
		gcode += line_F(3200);
		gcode += line_XYE(cleaning_box.rd.x , cleaning_box.rd.y, (cleaning_box.rd.x - cleaning_box.ld.x)*(extrusion_flow ));
		gcode += line_XYE(cleaning_box.ru.x , cleaning_box.ru.y, (cleaning_box.ru.y - cleaning_box.rd.y)*(extrusion_flow ));
		gcode += line_XYE(cleaning_box.lu.x , cleaning_box.lu.y, (cleaning_box.ru.x - cleaning_box.lu.x)*(extrusion_flow ));
		gcode += line_XYE(cleaning_box.ld.x, cleaning_box.ld.y, (cleaning_box.lu.y - cleaning_box.ld.y)*(extrusion_flow ));
		
		gcode += line_F(7200);
		gcode += line_XY(cleaning_box.rd.x, cleaning_box.ld.y);
		gcode += line_XY(cleaning_box.ld.x, cleaning_box.ld.y);

		gcode += line_F(6000);
		break;
	}

	return gcode;
}

std::string WipeTower::toolchange_Change(int tool, material_type /* current_material */, material_type new_material)
{
	assert(tool >= 0 && tool < 4);
	std::string gcode("T0\n");
	gcode[1] += char(tool);
	
	switch (new_material)
	{
	case PVA:
		gcode += line_M220(80);
		gcode += line_G4(0);
		break;
	case SCAFF:
		gcode += line_M220(35);
		gcode += line_G4(0);
		break;
	case FLEX:
		gcode += line_M220(35);
		gcode += line_G4(0);
		break;
	default:
		gcode += line_M220(100);
		gcode += line_G4(0);
		break;
	}

	return gcode;
}

std::string WipeTower::Perimeter(int order, int total, int Layer, bool afterToolchange, int firstLayerOffset)
{
	float _speed = 1.f;

	std::string gcode =
		";--------------------\n"
		"; CP EMPTY GRID START\n";
	gcode += line_CommentValue(" layer #", Layer);
	
	if (Layer == 20)
		_speed = 2.f;

	box_coordinates _p = _boxForColor(order);
	box_coordinates _to = _boxForColor(total);
	_p.ld.y += firstLayerOffset;
	_p.rd.y += firstLayerOffset;

	_p.lu = _to.lu; _p.ru = _to.ru;

	
	if (!afterToolchange)
	{
		gcode += line_EF(retract*(float)-1.5, 3600);
		gcode += line_ZF(m_z_pos + zHop, 7200);
		gcode += line_XYF(_p.ld.x + randi(5, 20), _p.ld.y, 7000);
		gcode += line_ZF(m_z_pos, 7200);
		gcode += line_XEF(_p.ld.x, retract*(float)1.5, 3600);
	}
	
	gcode += line_F(2400 / _speed);
	gcode += line_XYE(_p.lu.x, _p.ru.y, (_p.ru.y - _p.ld.y) * extrusion_flow);
	gcode += line_XYE(_p.ru.x, _p.ru.y, (_p.ru.x - _p.lu.x) * extrusion_flow);
	gcode += line_XYE(_p.rd.x, _p.rd.y, (_p.ru.y - _p.ld.y) * extrusion_flow);
	gcode += line_XYE(_p.ld.x + (perimeterWidth / 2), _p.ld.y, (_p.ru.x - _p.lu.x) * extrusion_flow);
	
	gcode += line_F(3200 / _speed);
	gcode += line_XYE(_p.lu.x + (perimeterWidth / 2), _p.lu.y - (perimeterWidth / 2), (_p.ru.y - _p.ld.y) * extrusion_flow);
	gcode += line_XYE(_p.ru.x - (perimeterWidth / 2), _p.ru.y - (perimeterWidth / 2), (_p.ru.x - _p.lu.x) * extrusion_flow);
	gcode += line_XYE(_p.rd.x - (perimeterWidth / 2), _p.rd.y + (perimeterWidth / 2), (_p.ru.y - _p.ld.y) * extrusion_flow);
	gcode += line_XYE(_p.ld.x + perimeterWidth, _p.ld.y + (perimeterWidth / 2), (_p.ru.x - _p.lu.x) * extrusion_flow);
	gcode += line_XYE(_p.ld.x + perimeterWidth, _p.ld.y + perimeterWidth, perimeterWidth * extrusion_flow);
	
	gcode += line_F(2900 / _speed);
	gcode += line_XYE(_p.ld.x + (perimeterWidth * 3), _p.ld.y + perimeterWidth, (perimeterWidth * 3) * extrusion_flow);
	gcode += line_XYE(_p.lu.x + (perimeterWidth * 3), _p.lu.y - perimeterWidth, (_p.ru.y - _p.ld.y) * extrusion_flow);
	gcode += line_XYE(_p.lu.x + (perimeterWidth * 6), _p.lu.y - perimeterWidth, (perimeterWidth * 3) * extrusion_flow);
	gcode += line_XYE(_p.ld.x + (perimeterWidth * 6), _p.ld.y + perimeterWidth, (_p.ru.y - _p.ld.y) * extrusion_flow);

	if (_p.lu.y - _p.ld.y > 4)
	{
		gcode += line_F(3200 / _speed);
		float _step = (m_wipe_tower_width - (perimeterWidth * 12)) / 3;
		float _sx = 0;
		for (int _s = 0; _s < 3; _s++)
		{
			float _ext = ((_p.ru.y - _p.ld.y) / 3)*(extrusion_flow*(float)1.5);
			_sx = _sx + (_step / 2);
			gcode += line_XYE(_p.ld.x + (perimeterWidth * 6) + (_step / 4) + (_step * _s), _p.ld.y + (perimeterWidth * 8), _ext);
			gcode += line_XYE(_p.ld.x + (perimeterWidth * 6) + (_step / 4) + (_step * _s), _p.lu.y - (perimeterWidth * 8), _ext);
			gcode += line_XYE(_p.ld.x + (perimeterWidth * 6) + ((_step / 4) * 2) + (_step * _s), _p.lu.y - perimeterWidth, _ext);
			gcode += line_XYE(_p.ld.x + (perimeterWidth * 6) + ((_step / 4) * 3) + (_step * _s), _p.lu.y - (perimeterWidth * 8), _ext);
			gcode += line_XYE(_p.ld.x + (perimeterWidth * 6) + ((_step / 4) * 3) + (_step * _s), _p.ld.y + (perimeterWidth * 8), _ext);
			gcode += line_XYE(_p.ld.x + (perimeterWidth * 6) + (_step)+(_step * _s), _p.ld.y + perimeterWidth, _ext);
		}
	}

	gcode += line_F(2900 / _speed);
	gcode += line_XYE(_p.ru.x - (perimeterWidth * 6), _p.ru.y - perimeterWidth, (_p.ru.y - _p.ld.y) * extrusion_flow);
	gcode += line_XYE(_p.ru.x - (perimeterWidth * 3), _p.ru.y - perimeterWidth, (perimeterWidth * 3) * extrusion_flow);
	gcode += line_XYE(_p.rd.x - (perimeterWidth * 3), _p.rd.y + perimeterWidth, (_p.ru.y - _p.ld.y) * extrusion_flow);
	gcode += line_XYE(_p.rd.x - perimeterWidth, _p.rd.y + perimeterWidth, perimeterWidth * extrusion_flow);

	gcode += line_F(7200);
	gcode += line_XY(_p.ld.x + perimeterWidth, _p.rd.y + (perimeterWidth / 2));
	gcode += line_XY(_p.rd.x - perimeterWidth, _p.ld.y + (perimeterWidth / 2));

	gcode += "; CP EMPTY GRID END\n"
			 ";------------------\n\n\n\n\n\n\n";

	return gcode;
}

WipeTower::box_coordinates WipeTower::_boxForColor(int order) const
{
	return box_coordinates(m_wipe_tower_pos.x, m_wipe_tower_pos.y + m_wipe_area * order - perimeterWidth / 2, m_wipe_tower_width, perimeterWidth);
}

}; // namespace PrusaSingleExtruderMM
