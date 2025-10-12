# Printer Kinematics Detection & Tall Print Speed Adjustment

## Overview

The AI optimization system now **intelligently detects printer kinematics** and adjusts speeds based on whether the bed moves (bed slinger) or stays stationary (CoreXY/Delta).

---

## Why This Matters

### Bed Slinger Problem (Cartesian/Prusa/Ender)
- **Bed moves in Y-axis** during printing
- **Inertial forces** increase with print height: `Force = mass × acceleration × height`
- **Risk**: Tall prints can tip over or separate from bed during rapid Y movements
- **Solution**: Reduce speed proportionally to height

### CoreXY/Delta Advantage
- **Bed stays stationary** (only Z-axis movement)
- **No inertial forces** from XY movements
- **Can print tall objects at full speed** (only vibration concerns)

---

## Kinematics Detection Logic

### Code Location
`src/libslic3r/UnifiedOfflineAI.cpp` lines 320-350

### Detection Method
Analyzes printer name/model strings for keywords:

```cpp
// CoreXY Detection (bed stationary)
if (printer_name.contains("corexy") || 
    printer_name.contains("voron") || 
    printer_name.contains("vzbot") || 
    printer_name.contains("hypercube") || 
    printer_name.contains("railcore")) {
    kinematics = CoreXY;
    is_bed_slinger = false;
}

// Delta Detection (bed stationary)
else if (printer_name.contains("delta") || 
         printer_name.contains("kossel") || 
         printer_name.contains("rostock")) {
    kinematics = Delta;
    is_bed_slinger = false;
}

// CoreXZ Detection (bed moves in Y)
else if (printer_name.contains("corexz")) {
    kinematics = CoreXZ;
    is_bed_slinger = true;
}

// Default: Cartesian bed slinger
else {
    kinematics = Cartesian;
    is_bed_slinger = true;  // Conservative default
}
```

---

## Speed Adjustment Formulas

### Bed Slinger (Cartesian/Prusa MK3/Ender 3/CR-10)

**Formula**: `speed_multiplier = 1.0 - (height_mm / 1000)`

**Examples**:
- **100mm tall**: `1.0 - 0.1 = 0.9x speed` (10% reduction)
- **150mm tall**: `1.0 - 0.15 = 0.85x speed` (15% reduction)
- **200mm tall**: `1.0 - 0.2 = 0.8x speed` (20% reduction)
- **300mm tall**: `1.0 - 0.3 = 0.7x speed` (30% reduction)

**Infill Adjustment**: `base_infill + 5% + (height / 50)`
- 100mm: 20% → 27%
- 200mm: 20% → 29%
- 300mm: 20% → 31%

**Physics Reasoning**:
- Moment arm increases linearly with height
- Torque = Force × Height
- Risk of tipping proportional to height
- Conservative reduction prevents knockovers

---

### CoreXY/Delta (Voron/VzBot/Kossel)

**Formula**: `speed_multiplier = 0.95` (constant, minimal reduction)

**Examples**:
- **100mm tall**: `0.95x speed` (5% reduction)
- **200mm tall**: `0.95x speed` (5% reduction)
- **300mm tall**: `0.95x speed` (5% reduction)

**Infill Adjustment**: `base_infill + 5%` (constant)

**Reasoning**:
- Bed doesn't move = no inertial forces on print
- Only minor reduction for frame vibration/resonance
- Height doesn't significantly impact print stability

---

## Real-World Examples

### Example 1: 250mm Tall Vase on Ender 3 (Bed Slinger)

**Without Kinematics Detection**:
- Speed: 60mm/s × 0.9 = 54mm/s (generic tall print reduction)
- Result: Still risky, might tip during fast Y movements

**With Kinematics Detection**:
- Detects: Cartesian bed slinger
- Speed: 60mm/s × (1.0 - 0.25) = 45mm/s (25% reduction)
- Infill: 20% + 5% + 5% = 30%
- Result: ✅ Safer, more rigid print

---

### Example 2: 250mm Tall Vase on Voron 2.4 (CoreXY)

**Without Kinematics Detection**:
- Speed: 60mm/s × 0.9 = 54mm/s (unnecessarily slow)
- Result: Works, but wastes time

**With Kinematics Detection**:
- Detects: CoreXY (bed stationary)
- Speed: 60mm/s × 0.95 = 57mm/s (minimal reduction)
- Infill: 20% + 5% = 25%
- Result: ✅ Fast AND safe, leverages CoreXY advantages

---

### Example 3: 150mm Part on Prusa MK3 (Bed Slinger)

**Detection**:
- Printer: "Prusa i3 MK3S" → Cartesian bed slinger
- Height: 150mm

**Adjustments**:
- Speed: 60mm/s × (1.0 - 0.15) = 51mm/s
- Infill: 20% + 5% + 3% = 28%
- Reasoning: "Bed slinger with tall print: reduced speed significantly to prevent knockover"

---

### Example 4: 150mm Part on Voron 2.4 (CoreXY)

**Detection**:
- Printer: "Voron 2.4" → CoreXY
- Height: 150mm

**Adjustments**:
- Speed: 60mm/s × 0.95 = 57mm/s
- Infill: 20% + 5% = 25%
- Reasoning: "CoreXY printer: minimal speed reduction for tall print (bed stationary)"

**Time Savings**: ~12% faster than treating it as bed slinger!

---

## Supported Printer Detection

### CoreXY (is_bed_slinger = false)
- Voron 0/1.8/2.4/Trident
- VzBot
- Hypercube/Hypercube Evolution
- RailCore II
- Any printer with "CoreXY" in name

### Delta (is_bed_slinger = false)
- Kossel (Mini/XL/Pro)
- Rostock
- Any printer with "Delta" in name

### Cartesian Bed Slinger (is_bed_slinger = true)
- Ender 3/5/6/7
- Creality CR-10/CR-20/CR-30
- Prusa i3 MK2/MK3/MK4
- Anycubic i3 Mega/Vyper
- Artillery Sidewinder/Genius
- **Default for unknown printers** (conservative)

### CoreXZ (is_bed_slinger = true)
- Bed moves in Y, so still subject to inertial forces
- Treated like bed slinger for tall prints

---

## Code Implementation

### PrinterCapabilities Struct
```cpp
struct PrinterCapabilities {
    enum class Kinematics { 
        Cartesian,  // XY gantry or bed
        CoreXY,     // XY motors drive both axes
        CoreXZ,     // XZ motors share axes
        Delta,      // Parallel arm kinematics
        Unknown 
    };
    
    Kinematics kinematics = Kinematics::Cartesian;
    bool is_bed_slinger = true;  // Default: conservative
};
```

### Geometry Adjustment Function
```cpp
void apply_geometry_adjustments(MaterialPreset& preset,
                                const GeometryFeatures& features) {
    if (features.height > 100.0) {
        if (m_printer_caps.is_bed_slinger) {
            // Aggressive speed reduction
            double height_penalty = std::min(0.3, features.height / 1000.0);
            preset.print_speed *= (1.0 - height_penalty);
            preset.infill += 5.0 + (features.height / 50.0);
            reasoning = "Bed slinger with tall print: reduced speed significantly";
        } else {
            // Minimal reduction (frame vibration only)
            preset.print_speed *= 0.95;
            preset.infill += 5.0;
            reasoning = "CoreXY/Delta: minimal speed reduction (bed stationary)";
        }
    }
}
```

---

## Benefits

### For Bed Slingers:
✅ **Safer tall prints** - dramatic speed reduction prevents knockovers  
✅ **Stronger prints** - increased infill adds rigidity  
✅ **Fewer failures** - conservative approach avoids bed adhesion issues  

### For CoreXY/Delta:
✅ **Faster prints** - minimal speed penalty for tall objects  
✅ **Time savings** - 10-25% faster than treating as bed slinger  
✅ **Better performance** - leverages kinematic advantages  

### For All Users:
✅ **Automatic detection** - no manual configuration needed  
✅ **Explainable** - reasoning appears in logs  
✅ **Conservative defaults** - unknown printers treated as bed slingers  

---

## Testing Recommendations

### Test Case 1: Bed Slinger Verification
1. Load profile: "Ender 3 V2" or "Prusa i3 MK3S"
2. Import tall model: 200mm+ height
3. Enable AI mode and slice
4. **Verify in logs**:
   - "Bed slinger with tall print: reduced speed significantly"
   - Speed reduced by ~20%
   - Infill increased to ~29%

### Test Case 2: CoreXY Verification
1. Load profile: "Voron 2.4" or create CoreXY printer
2. Import same tall model
3. Enable AI mode and slice
4. **Verify in logs**:
   - "CoreXY printer: minimal speed reduction (bed stationary)"
   - Speed reduced by only ~5%
   - Faster than bed slinger by ~15-20%

### Test Case 3: Unknown Printer (Conservative)
1. Create custom printer named "My DIY Printer"
2. Import tall model
3. **Verify defaults to bed slinger** (safe behavior)

---

## Future Enhancements

Possible additions if needed:

1. **Acceleration-based formula**: Use printer's max acceleration to calculate safe speeds
2. **Mass estimation**: Factor in model volume/weight for more accurate inertial calculations
3. **Bed material detection**: Glass beds (heavy) vs spring steel (light) have different dynamics
4. **User override**: Allow expert users to manually set kinematics type
5. **Calibration mode**: Learn optimal speeds from successful tall prints

---

## Physics Background

### Inertial Force on Bed Slinger
```
F = m × a
Torque = F × h (height)
Tipping_Risk = Torque / Base_Area
```

Where:
- `m` = print mass
- `a` = bed acceleration
- `h` = print height (moment arm)

**Tipping occurs when**: `Torque > (Weight × Base_Width / 2)`

### Why Height Matters
- **2x height = 2x torque** (linear relationship)
- **Same acceleration = 2x tipping risk**
- **Solution: Reduce acceleration (slower speeds)**

### Why CoreXY Doesn't Care
- Bed doesn't move in XY
- Only Z movement (slow, vertical)
- No lateral forces on print
- Height irrelevant for stability

---

*This feature makes your auto slicer **mechanically aware** and printer-specific!* 🚀
