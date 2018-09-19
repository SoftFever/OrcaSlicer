/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2000-2004  Brian S. Dean <bsd@bsdhome.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/* $Id: pindefs.h 1132 2013-01-09 19:23:30Z rliebscher $ */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "avrdude.h"
#include "libavrdude.h"

/**
 * Adds a pin in the pin definition as normal or inverse pin.
 *
 * @param[out] pindef pin definition to update
 * @param[in] pin number of pin [0..PIN_MAX]
 * @param[in] inverse inverse (true) or normal (false) pin
 */
void pin_set_value(struct pindef_t * const pindef, const int pin, const bool inverse) {

  pindef->mask[pin / PIN_FIELD_ELEMENT_SIZE] |= 1 << (pin % PIN_FIELD_ELEMENT_SIZE);
  if(inverse) {
    pindef->inverse[pin / PIN_FIELD_ELEMENT_SIZE] |= (1 << (pin % PIN_FIELD_ELEMENT_SIZE));
  } else {
    pindef->inverse[pin / PIN_FIELD_ELEMENT_SIZE] &= ~(1 << (pin % PIN_FIELD_ELEMENT_SIZE));
  }
}

/**
 * Clear all defined pins in pindef.
 *
 * @param[out] pindef pin definition to clear
 */
void pin_clear_all(struct pindef_t * const pindef) {
  memset(pindef, 0, sizeof(struct pindef_t));
}

/**
 * Convert new pin definition to old pin number
 *
 * @param[in] pindef new pin definition structure
 * @param[out] pinno old pin definition integer
 */
static int pin_fill_old_pinno(const struct pindef_t * const pindef, unsigned int * const pinno) {
  bool found = false;
  int i;
  for(i = 0; i < PIN_MAX; i++) {
    if(pindef->mask[i / PIN_FIELD_ELEMENT_SIZE] & (1 << (i % PIN_FIELD_ELEMENT_SIZE))) {
      if(found) {
        avrdude_message(MSG_INFO, "Multiple pins found\n"); //TODO
        return -1;
      }
      found = true;
      *pinno = i;
      if(pindef->inverse[i / PIN_FIELD_ELEMENT_SIZE] & (1 << (i % PIN_FIELD_ELEMENT_SIZE))) {
        *pinno |= PIN_INVERSE;
      }
    }
  }
  return 0;
}

/**
 * Convert new pin definition to old pinlist, does not support mixed inverted/non-inverted pin
 *
 * @param[in] pindef new pin definition structure
 * @param[out] pinno old pin definition integer
 */
static int pin_fill_old_pinlist(const struct pindef_t * const pindef, unsigned int * const pinno) {
  int i;

  for(i = 0; i < PIN_FIELD_SIZE; i++) {
    if(i == 0) {
      if((pindef->mask[i] & ~PIN_MASK) != 0) {
        avrdude_message(MSG_INFO, "Pins of higher index than max field size for old pinno found\n");
        return -1;
      }
      if (pindef->mask[i] == 0) {
        /* this pin function is not using any pins */
        *pinno = 0;
      } else if(pindef->mask[i] == pindef->inverse[i]) {  /* all set bits in mask are set in inverse */
        *pinno = pindef->mask[i];
        *pinno |= PIN_INVERSE;
      } else if(pindef->mask[i] == ((~pindef->inverse[i]) & pindef->mask[i])) {  /* all set bits in mask are cleared in inverse */
        *pinno = pindef->mask[i];
      } else {
        avrdude_message(MSG_INFO, "pins have different polarity set\n");
        return -1;
      }
    } else if(pindef->mask[i] != 0) {
      avrdude_message(MSG_INFO, "Pins have higher number than fit in old format\n");
      return -1;
    }
  }
  return 0;
}


/**
 * Convert for given programmer new pin definitions to old pin definitions.
 *
 * @param[inout] pgm programmer whose pins shall be converted.
 */
int pgm_fill_old_pins(struct programmer_t * const pgm) {

  if (pin_fill_old_pinlist(&(pgm->pin[PPI_AVR_VCC]),  &(pgm->pinno[PPI_AVR_VCC])) < 0)
    return -1;
  if (pin_fill_old_pinlist(&(pgm->pin[PPI_AVR_BUFF]), &(pgm->pinno[PPI_AVR_BUFF])) < 0)
    return -1;
  if (pin_fill_old_pinno(&(pgm->pin[PIN_AVR_RESET]), &(pgm->pinno[PIN_AVR_RESET])) < 0)
    return -1;
  if (pin_fill_old_pinno(&(pgm->pin[PIN_AVR_SCK]),  &(pgm->pinno[PIN_AVR_SCK])) < 0)
    return -1;
  if (pin_fill_old_pinno(&(pgm->pin[PIN_AVR_MOSI]), &(pgm->pinno[PIN_AVR_MOSI])) < 0)
    return -1;
  if (pin_fill_old_pinno(&(pgm->pin[PIN_AVR_MISO]), &(pgm->pinno[PIN_AVR_MISO])) < 0)
    return -1;
  if (pin_fill_old_pinno(&(pgm->pin[PIN_LED_ERR]),  &(pgm->pinno[PIN_LED_ERR])) < 0)
    return -1;
  if (pin_fill_old_pinno(&(pgm->pin[PIN_LED_RDY]),  &(pgm->pinno[PIN_LED_RDY])) < 0)
    return -1;
  if (pin_fill_old_pinno(&(pgm->pin[PIN_LED_PGM]),  &(pgm->pinno[PIN_LED_PGM])) < 0)
    return -1;
  if (pin_fill_old_pinno(&(pgm->pin[PIN_LED_VFY]),  &(pgm->pinno[PIN_LED_VFY])) < 0)
    return -1;

  return 0;
}

/**
 * This function returns a string representation of pins in the mask eg. 1,3,5-7,9,12
 * Another execution of this function will overwrite the previous result in the static buffer.
 * Consecutive pin number are representated as start-end.
 *
 * @param[in] pinmask the pin mask for which we want the string representation
 * @returns pointer to a static string.
 */
const char * pinmask_to_str(const pinmask_t * const pinmask) {
  static char buf[(PIN_MAX + 1) * 5]; // should be enough for PIN_MAX=255
  char *p = buf;
  int n;
  int pin;
  const char * fmt;
  int start = -1;
  int end = -1;

  buf[0] = 0;
  for(pin = PIN_MIN; pin <= PIN_MAX; pin++) {
    int index = pin / PIN_FIELD_ELEMENT_SIZE;
    int bit = pin % PIN_FIELD_ELEMENT_SIZE;
    if(pinmask[index] & (1 << bit)) {
      bool output = false;
      if(start == -1) {
        output = true;
        start = pin;
        end = start;
      } else if(pin == end + 1) {
        end = pin;
      } else {
        if(start != end) {
          n = sprintf(p, "-%d", end);
          p += n;
        }
        output = true;
        start = pin;
        end = start;
      }
      if(output) {
        fmt = (buf[0] == 0) ? "%d" : ",%d";
        n = sprintf(p, fmt, pin);
        p += n;
      }
    }
  }
  if(start != end) {
    n = sprintf(p, "-%d", end);
    p += n;
  }

  if(buf[0] == 0)
    return  "(no pins)";

  return buf;
}


/**
 * This function checks all pin of pgm against the constraints given in the checklist.
 * It checks if 
 * @li any invalid pins are used
 * @li valid pins are used inverted when not allowed
 * @li any pins are used by more than one function
 * @li any mandatory pin is not set all.
 *
 * In case of any error it report the wrong function and the pin numbers.
 * For verbose >= 2 it also reports the possible correct values.
 * For verbose >=3 it shows also which pins were ok.
 *
 * @param[in] pgm the programmer to check
 * @param[in] checklist the constraint for the pins
 * @param[in] size the number of entries in checklist
 * @returns 0 if all pin definitions are valid, -1 otherwise
 */
int pins_check(const struct programmer_t * const pgm, const struct pin_checklist_t * const checklist, const int size, bool output) {
  static const struct pindef_t no_valid_pins = {{0}, {0}}; // default value if check list does not contain anything else
  int rv = 0; // return value
  int pinname; // loop counter through pinnames
  pinmask_t already_used_all[PIN_FIELD_SIZE] = {0}; // collect pin definitions of all pin names for check of double use
  // loop over all possible pinnames
  for(pinname = 0; pinname < N_PINS; pinname++) {
    bool used = false;
    bool invalid = false;
    bool inverse = false;
    int index;
    int segment;
    bool mandatory_used = false;
    pinmask_t invalid_used[PIN_FIELD_SIZE] = {0};
    pinmask_t inverse_used[PIN_FIELD_SIZE] = {0};
    pinmask_t already_used[PIN_FIELD_SIZE] = {0};
    const struct pindef_t * valid_pins = &no_valid_pins;
    bool is_mandatory = false;
    bool is_ok = true;
    //find corresponding check pattern
    for(index = 0; index < size; index++) {
      if(checklist[index].pinname == pinname) {
        valid_pins = checklist[index].valid_pins;
        is_mandatory = checklist[index].mandatory;
        break;
      }
    }

    for(segment = 0; segment < PIN_FIELD_SIZE; segment++) {
      // check if for mandatory any pin is defined
      invalid_used[segment] = pgm->pin[pinname].mask[segment] & ~valid_pins->mask[segment];
      if(is_mandatory && (0 != (pgm->pin[pinname].mask[segment] & valid_pins->mask[segment]))) {
        mandatory_used = true;
      }
      // check if it does not use any non valid pins
      invalid_used[segment] = pgm->pin[pinname].mask[segment] & ~valid_pins->mask[segment];
      if(invalid_used[segment]) {
        invalid = true;
      }
      // check if it does not use any valid pins as inverse if not allowed
      inverse_used[segment] = pgm->pin[pinname].inverse[segment] & valid_pins->mask[segment] & ~valid_pins->inverse[segment];
      if(inverse_used[segment]) {
        inverse = true;
      }
      // check if it does not use same pins as other function
      already_used[segment] = pgm->pin[pinname].mask[segment] & already_used_all[segment];
      if(already_used[segment]) {
        used = true;
      }
      already_used_all[segment] |= pgm->pin[pinname].mask[segment];
    }
    if(invalid) {
      if(output) {
        avrdude_message(MSG_INFO, "%s: %s: Following pins are not valid pins for this function: %s\n",
                        progname, avr_pin_name(pinname), pinmask_to_str(invalid_used));
        avrdude_message(MSG_NOTICE2, "%s: %s: Valid pins for this function are: %s\n",
                  progname, avr_pin_name(pinname), pinmask_to_str(valid_pins->mask));
      }
      is_ok = false;
    }
    if(inverse) {
      if(output) {
        avrdude_message(MSG_INFO, "%s: %s: Following pins are not usable as inverse pins for this function: %s\n",
                        progname, avr_pin_name(pinname), pinmask_to_str(inverse_used));
        avrdude_message(MSG_NOTICE2, "%s: %s: Valid inverse pins for this function are: %s\n",
                          progname, avr_pin_name(pinname), pinmask_to_str(valid_pins->inverse));
      }
      is_ok = false;
    }
    if(used) {
      if(output) {
        avrdude_message(MSG_INFO, "%s: %s: Following pins are set for other functions too: %s\n",
                        progname, avr_pin_name(pinname), pinmask_to_str(already_used));
        is_ok = false;
      }
    }
    if(!mandatory_used && is_mandatory && !invalid) {
      if(output) {
        avrdude_message(MSG_INFO, "%s: %s: Mandatory pin is not defined.\n",
                        progname, avr_pin_name(pinname));
      }
      is_ok = false;
    }
    if(!is_ok) {
      rv = -1;
    } else if(output) {
      avrdude_message(MSG_DEBUG, "%s: %s: Pin is ok.\n",
                      progname, avr_pin_name(pinname));
    }
  }
  return rv;
}

/**
 * This function returns a string representation of defined pins eg. ~1,2,~4,~5,7
 * Another execution of this function will overwrite the previous result in the static buffer.
 *
 * @param[in] pindef the pin definition for which we want the string representation
 * @returns pointer to a static string.
 */
const char * pins_to_str(const struct pindef_t * const pindef) {
  static char buf[(PIN_MAX + 1) * 5]; // should be enough for PIN_MAX=255
  char *p = buf;
  int n;
  int pin;
  const char * fmt;

  buf[0] = 0;
  for(pin = PIN_MIN; pin <= PIN_MAX; pin++) {
    int index = pin / PIN_FIELD_ELEMENT_SIZE;
    int bit = pin % PIN_FIELD_ELEMENT_SIZE;
    if(pindef->mask[index] & (1 << bit)) {
      if(pindef->inverse[index] & (1 << bit)) {
        fmt = (buf[0] == 0) ? "~%d" : ",~%d";
      } else {
        fmt = (buf[0] == 0) ? " %d" : ",%d";
      }
      n = sprintf(p, fmt, pin);
      p += n;
    }
  }

  if(buf[0] == 0)
    return " (not used)";

  return buf;
}

/**
 * Returns the name of the pin as string.
 *
 * @param pinname the pinname which we want as string.
 * @returns a string with the pinname, or <unknown> if pinname is invalid.
 */
const char * avr_pin_name(int pinname) {
  switch(pinname) {
  case PPI_AVR_VCC   : return "VCC";
  case PPI_AVR_BUFF  : return "BUFF";
  case PIN_AVR_RESET : return "RESET";
  case PIN_AVR_SCK   : return "SCK";
  case PIN_AVR_MOSI  : return "MOSI";
  case PIN_AVR_MISO  : return "MISO";
  case PIN_LED_ERR   : return "ERRLED";
  case PIN_LED_RDY   : return "RDYLED";
  case PIN_LED_PGM   : return "PGMLED";
  case PIN_LED_VFY   : return "VFYLED";
  default : return "<unknown>";
  }
}


