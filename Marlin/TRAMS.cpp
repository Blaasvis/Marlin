#include <stdio.h>
#include <avr/io.h>
#include "MarlinConfig.h"
#include "TRAMS.h"
#include <SPI.h>

#include "endstops.h"

#if ENABLED(IS_TRAMS)
  Trams stepper;
  TramsEndstops endstops;
#endif

#define ENABLE_STEPPER_DRIVER_INTERRUPT()  SBI(TIMSK1, OCIE1A)
#define DISABLE_STEPPER_DRIVER_INTERRUPT() CBI(TIMSK1, OCIE1A)

void setCSPin(const AxisEnum &axis, bool state) {
  switch(axis) {
    case X_AXIS: WRITE( X_CS_PIN, state); break;
    case Y_AXIS: WRITE( Y_CS_PIN, state); break;
    case Z_AXIS: WRITE( Z_CS_PIN, state); break;
    case E_AXIS: WRITE(E0_CS_PIN, state); break;
  }
}

/**
 * @brief Initialize the SPI
 * SPI Master
 * 4Mhz(CPU_CLOCK / 4)
 * CPOL = 0
 * CPHA = 0
 * no interrupt
 */
void TramsSPI::spi_init(void) {
  SPI.begin();

  SET_OUTPUT(X_CS_PIN);
  SET_OUTPUT(Y_CS_PIN);
  SET_OUTPUT(Z_CS_PIN);
  SET_OUTPUT(E0_CS_PIN);
  WRITE(X_CS_PIN, HIGH);
  WRITE(Y_CS_PIN, HIGH);
  WRITE(Z_CS_PIN, HIGH);
  WRITE(E0_CS_PIN, HIGH);
}

/**
 * @brief Reads four byte via SPI
 * @param		address		register address
 * @param		csPin		chip select
 * @return		status
 */
uint32_t TramsSPI::spi_readRegister(uint8_t address, const AxisEnum &axis) {
  uint8_t buf[4];
  uint32_t register_value = 0;

  SPI.beginTransaction(SPISettings(SPI_SPEED, MSBFIRST, SPI_MODE3));
  setCSPin(axis, LOW);

  // first read cycle to address the register
  SPI.transfer(address);
  SPI.transfer(0x00);
  SPI.transfer(0x00);
  SPI.transfer(0x00);
  SPI.transfer(0x00);

  setCSPin(axis, HIGH);

  DELAY_3_NOP;

  setCSPin(axis, LOW);

  // second read cycle to get the register value
  SPI.transfer(address);
  buf[3] = SPI.transfer(0x00);
  buf[2] = SPI.transfer(0x00);
  buf[1] = SPI.transfer(0x00);
  buf[0] = SPI.transfer(0x00);

  setCSPin(axis, HIGH);
  SPI.endTransaction();

  register_value |= buf[3];
  register_value = register_value << 8;
  register_value |= buf[2];
  register_value = register_value << 8;
  register_value |= buf[1];
  register_value = register_value << 8;
  register_value |= buf[0];

  return register_value;
}

/**
 * @brief Send five byte via SPI and received the status
 * @param		address		register address
 * @param		data		to transmitted data
 * @param		csPin		chip select
 * @return	status
 */
uint8_t TramsSPI::spi_writeRegister(const uint8_t address, const uint32_t data, const AxisEnum &axis) {
  uint8_t buf[4];
  uint8_t status = 0;

  buf[0] = data & 0xFF;
  buf[1] = (data & 0xFF00) >> 8;
  buf[2] = (data & 0xFF0000) >> 16;
  buf[3] = (data & 0xFF000000) >> 24;

  SPI.beginTransaction(SPISettings(SPI_SPEED, MSBFIRST, SPI_MODE3));
  setCSPin(axis, LOW);

  // address register
  SPI.transfer(address | READ_ACCESS);
  // send new register value
  SPI.transfer(buf[3]);
  SPI.transfer(buf[2]);
  SPI.transfer(buf[1]);
  SPI.transfer(buf[0]);

  setCSPin(axis, HIGH);
  SPI.endTransaction();

  return status;
}


/**
 * @brief Reads the status from the MAMC
 * @param		csPin		chip select
 * @return	status
 */
uint8_t TramsSPI::spi_readStatus(const AxisEnum &axis) {
  uint8_t status;

  SPI.beginTransaction(SPISettings(SPI_SPEED, MSBFIRST, SPI_MODE3));
  setCSPin(axis, LOW);

  // send adress and read the status from FPGA
  status = SPI.transfer(GCONF);	// addressing any register int the tmc5130
  // send data, msb first
  SPI.transfer(0x00);
  SPI.transfer(0x00);
  SPI.transfer(0x00);
  SPI.transfer(0x00);

  setCSPin(axis, HIGH);
  SPI.endTransaction();

  return status;
}

/**
 * @brief Activates the driver stage for the given axis
 * @param	axis		axis
 * @return	none
 */
void Trams::TMC5130_enableDriver(const AxisEnum &axis) {
	// low activ
	switch(axis){
		case X_AXIS: enable_X(); break;
		case Y_AXIS: enable_Y(); break;
		case Z_AXIS: enable_Z(); break;
		case E_AXIS: enable_E0(); break;
		default: break;
	}

	return;
}

/**
 * @brief Performs a homing for the given axis
 * @param	axis		axis to home
 * @return	none
 */
void Trams::TMC5130_homing(const AxisEnum &axis, const float homing_feedrate_mm_s) {
  AxisEnum axis_to_home;
  uint8_t motor_direction;
	uint16_t sw_register;
	uint32_t stallguardthreshold,
           stall_speed;
	int homing_retract,
	    homing_speed,
      homing_bump_speed;
	bool sg_active = false;

	//float STEPS_PER_UNIT_TMP[] = DEFAULT_AXIS_STEPS_PER_UNIT; // configuraton.h
	//float HOMING_FEEDRATE_TMP[] = { HOMING_FEEDRATE_XY, HOMING_FEEDRATE_XY, HOMING_FEEDRATE_Z };
  uint8_t bump_divisor[] = HOMING_BUMP_DIVISOR;

	switch(axis) {
		case X_AXIS:
			axis_to_home = X_AXIS;
			homing_retract = X_HOME_BUMP_MM * planner.axis_steps_per_mm[axis]; // configuraton_adv.h
			homing_speed = (int)homing_feedrate_mm_s * planner.axis_steps_per_mm[axis];
			homing_bump_speed = homing_speed / bump_divisor[axis];
			sw_register = SWITCH_POSITION_X | SWITCH_POLARITY_X; // TMC_TRAMS_CONFIGURATION

			#ifdef STALLGUARD_X
				sg_active = true;
				stallguardthreshold = STALLGUARDTHRESHOLD_X;
				motor_direction = STEPPER_DIRECTION_X;
			#endif

			break;
		case Y_AXIS:
			axis_to_home = Y_AXIS;
			homing_retract = Y_HOME_BUMP_MM * planner.axis_steps_per_mm[axis];
			homing_speed = (int)homing_feedrate_mm_s * planner.axis_steps_per_mm[axis];
			homing_bump_speed = homing_speed / bump_divisor[axis];
			sw_register = SWITCH_POSITION_Y | SWITCH_POLARITY_Y;

			#ifdef STALLGUARD_Y
				sg_active = true;
				stallguardthreshold = STALLGUARDTHRESHOLD_Y;
				motor_direction = STEPPER_DIRECTION_Y;
			#endif

			break;
		case Z_AXIS:
			axis_to_home = Z_AXIS;
			homing_retract = Z_HOME_BUMP_MM * planner.axis_steps_per_mm[axis];
			homing_speed = (int)homing_feedrate_mm_s * planner.axis_steps_per_mm[axis];
			homing_bump_speed = homing_speed / bump_divisor[Z_AXIS];
			sw_register = SWITCH_POSITION_Z | SWITCH_POLARITY_Z;

			#ifdef STALLGUARD_Z
				sg_active = true;
				stallguardthreshold = STALLGUARDTHRESHOLD_Z;
				motor_direction = STEPPER_DIRECTION_Z;
			#endif

			break;
		default:
			return;
	}

  #if ENABLED(DEBUG_LEVELING_FEATURE)
    if (DEBUGGING(LEVELING)) {
      SERIAL_ECHOLN("TMC HOMING:");
      SERIAL_ECHOLNPAIR("> axis_to_home = ", axis_to_home);
      SERIAL_ECHOLNPAIR("> homing_retract = ", homing_retract);
      SERIAL_ECHOLNPAIR("> homing_speed = ", homing_speed);
      SERIAL_ECHOLNPAIR("> homing_bump_speed = ", homing_bump_speed);
      SERIAL_ECHOLNPAIR("> sw_register = ", sw_register);

      #ifdef STALLGUARD_Y
        SERIAL_ECHOLNPAIR("> sg_active = ", sg_active);
        SERIAL_ECHOLNPAIR("> stallguardthreshold = ", stallguardthreshold);
        SERIAL_ECHOLNPAIR("> motor_direction = ", motor_direction);
      #endif
      SERIAL_EOL;
    }
  #endif

	// Homing Procedure:
	// Enable Trinamic Drivers to start homing movement

	if(sg_active == true) {
		spi_writeRegister(SW_MODE, 0x00, axis_to_home);	//SWITCH REGISTER

		stall_speed = 16777216 / homing_speed;
		stall_speed = stall_speed / 16;  // match homing speed to actual microstep speed (at 1/16 microstep)
		stall_speed = stall_speed * 1.10; // Activate stallGuard sligthly below desired homing velocity (provide 10% tolerance)

		spi_writeRegister(GCONF, 0x1080 | motor_direction, axis_to_home);	//stealthchop off for stallguard homing
		spi_writeRegister(COOLCONF, ((stallguardthreshold & 0x7F)<<16),axis_to_home);//sgt <-- Entry the value determined for SGT: lower value=higher sensitivity (lower force for stall detection)
		spi_writeRegister(TCOOLTHRS, stall_speed  ,axis_to_home);//TCOOLTHRS
		spi_writeRegister(SW_MODE, 0x400, axis_to_home);	//SWITCH REGISTER
		spi_writeRegister(AMAX, 100, axis_to_home);	//AMAX for stallGuard homing shall be significantly lower than AMAX for printing

		// Set velocity mode in direction to the endstop
		spi_writeRegister(RAMPMODE, VELOCITY_MODE_NEG, axis_to_home);	//VELOCITY MODE, direction to the endstop
		spi_writeRegister(VMAX, homing_speed, axis_to_home);	//Homing Speed in VMAX

		_delay_ms(20);

		//While motor is still moving (vzero != 1)
		while((spi_readRegister(RAMP_STAT, axis_to_home) & VZERO) != VZERO);

		// Endstop reached. Reset and retract
		spi_writeRegister(RAMPMODE, HOLD_MODE, axis_to_home);		//HOLD Mode
		spi_writeRegister(XACTUAL, 0x0, axis_to_home);				//XACTUAL = 0
		spi_writeRegister(XTARGET, 0x0, axis_to_home);				//XTARGET = 0
		spi_writeRegister(SW_MODE, 0x0, axis_to_home);				//SWITCH REGISTER
		spi_writeRegister(RAMPMODE, POSITIONING_MODE, axis_to_home);//Position MODE
		spi_writeRegister(VMAX, homing_speed, axis_to_home);	//Homing Speed in VMAX
		spi_writeRegister(DMAX, 0xFFFF, axis_to_home);				//DMAX
		spi_writeRegister(XTARGET, homing_retract, axis_to_home);	//XTARGET = homing_retract

		_delay_ms(20);

		//While motor is still moving (vzero != 1)
		while((spi_readRegister(RAMP_STAT, axis_to_home) & VZERO) != VZERO);

		// Endstop reached. Reset and retract
		spi_writeRegister(SW_MODE, 0x0, axis_to_home);	//SWITCH REGISTER
		spi_writeRegister(RAMPMODE, 0x3, axis_to_home);	//HOLD Mode
		spi_writeRegister(GCONF, 0x1080 | motor_direction, axis_to_home);//Turn on stealthchop again
		spi_writeRegister(XACTUAL, 0x0, axis_to_home);	//XACTUAL = 0
		spi_writeRegister(XTARGET, 0x0, axis_to_home);	//XTARGET = 0
		spi_writeRegister(RAMPMODE, 0x0, axis_to_home);	//Position MODE
		_delay_ms(200);
	} else {
		TMC5130_enableDriver(axis);

		// Set velocity mode in direction to the endstop
		spi_writeRegister(RAMPMODE, VELOCITY_MODE_NEG, axis_to_home);	//VELOCITY MODE negative Direction
		spi_writeRegister(VMAX, homing_speed, axis_to_home);			//Homing Speed in VMAX

		//Config switch register of TMC5130
		spi_writeRegister(SW_MODE, sw_register, axis_to_home);		//SWITCH REGISTER

		//While motor is still moving (vzero != 1)
		while((spi_readRegister(RAMP_STAT, axis_to_home) & VZERO) != VZERO) { idle(); }

		spi_writeRegister(RAMPMODE, HOLD_MODE,        axis_to_home); // HOLD Mode
		spi_writeRegister(XACTUAL,  0x0,              axis_to_home); // XACTUAL = 0
		spi_writeRegister(XTARGET,  0x0,              axis_to_home); // XTARGET = 0
    spi_writeRegister(SW_MODE,  0x0,              axis_to_home); // SWITCH REGISTER
    spi_writeRegister(RAMPMODE, POSITIONING_MODE, axis_to_home); // Position MODE
		spi_writeRegister(VMAX,     homing_speed,     axis_to_home); // Homing Speed in VMAX
		spi_writeRegister(DMAX,     0xFFFF,           axis_to_home); // DMAX
		spi_writeRegister(XTARGET,  homing_retract,   axis_to_home); // XTARGET = homing_retract

		_delay_ms(200);

		//While motor is still moving (vzero != 1)
		while((spi_readRegister(RAMP_STAT, axis_to_home) & VZERO) != VZERO) { idle(); }

		//Retract finished
		spi_writeRegister(SW_MODE,  sw_register,      axis_to_home); // SWITCH REGISTER
		spi_writeRegister(RAMPMODE, HOLD_MODE,        axis_to_home); // HOLD Mode
		spi_writeRegister(XACTUAL,  0x0,              axis_to_home); // XACTUAL = 0
		spi_writeRegister(XTARGET,  0x0,              axis_to_home); // XTARGET = 0
		spi_writeRegister(RAMPMODE, POSITIONING_MODE, axis_to_home); // Position MODE
	}
}

// Additional motion execution queue for use with Trinamic TMC5130
#define MOTION_BUFFER_SIZE 16
// Conversion of units between TMC5130 and Arduino
#define TMC5130clockFrequency (double)16000000
#define TMC5130_a_divisor (long)128
#define TMC5130_t_factor (double)1.048576

static Trams::motion_block_t motion_buffer[MOTION_BUFFER_SIZE],  // A ring buffer for motion movements
                      motion_buffer_block_old;
static volatile unsigned char motion_buffer_head = 0,           // Index of the next block to be pushed
                              motion_buffer_tail = 0;
static volatile bool motion_buffer_full = false;
static unsigned char out_bits;        // The next stepping-bits to be output
volatile long pos[NUM_AXIS] = { 0 };
static volatile long timerClk, nextTimerClk;
static volatile double TEMPtimerClk;

/**
 * @brief Called when the current block is no longer needed. Discards the block and makes the memory
 * available for new blocks.)
 * @return  none
 */
FORCE_INLINE void Trams::discard_current_motion_block(void) {
  motion_buffer_tail = (motion_buffer_tail + 1) & (MOTION_BUFFER_SIZE - 1);
  motion_buffer_full = false;
}


/**
 * @brief  Gets the current block. Returns NULL if buffer empty
 * @return pointer of the current block, null if empty
 */
FORCE_INLINE Trams::motion_block_t *Trams::get_current_motion_block(void) {
  if ((motion_buffer_head == motion_buffer_tail) && (motion_buffer_full == false)) {
    return(NULL);
  }
  else
    return &motion_buffer[motion_buffer_tail];
}


/**
 * @brief  Returns true if the buffer has a queued block, false otherwise
 * @return  false if queue emty, else true
 */
FORCE_INLINE bool Trams::motion_blocks_queued(void) {
  return ((motion_buffer_head != motion_buffer_tail) || motion_buffer_full);
}


/**
 * @brief  Returns a pointer to the next free position in the motion buffer, if there is one
 * @return  pointer of next free block
 */
FORCE_INLINE Trams::motion_block_t *Trams::get_next_free_motion_block(void) {
  if(motion_buffer_full)
    return NULL;
  else
    return &motion_buffer[motion_buffer_head];
}


/**
 * @brief  Increases the head pointer of the buffer. Don't use without proving
 * before that the queue is not full
 * @return  none
 */
FORCE_INLINE void Trams::append_motion_block(void) {
  CRITICAL_SECTION_START;
  motion_buffer_head += 1;
  if(motion_buffer_head == MOTION_BUFFER_SIZE)
    motion_buffer_head = 0;
  if(motion_buffer_head == motion_buffer_tail)
    motion_buffer_full = true;
  CRITICAL_SECTION_END;
}

/**
 * @brief  Returns the number of queued blocks
 * @return  number of queued blocks
 */
unsigned char Trams::blocks_in_motion_queue() {
  char temp = motion_buffer_head - motion_buffer_tail;
  if(temp < 0)
    temp += MOTION_BUFFER_SIZE;
  return temp;
}

/**
 * @brief Pre-calculates the parameters for the current block, in order to save time. Hence, in the interrupt function
 * there is no processing, only sending the data via SPI
 * @return none
 */
void Trams::calculate(void) {
  unsigned long scale_axis; // scale factor
  Trams::motion_block_t *current_motion_block = get_next_free_motion_block();

  // block in motion_block available?
  if(current_motion_block == NULL) return;

  // If there is no current block, attempt to pop one from the buffer
  current_block = planner.get_current_block();
  // Anything in the buffer?
  if (current_block == NULL) return;
  SBI(current_block->flag, BLOCK_BIT_BUSY); //Block being used

  // Set directions
  out_bits = current_block->direction_bits;

  // Calculate ramp parameters of the current block
  // If there is any movement in the x-axis
  if(current_block->steps[X_AXIS] != 0) {
    //accel[X_AXIS] = current_block->steps[X_AXIS] * current_block->acceleration_steps_per_s2 / current_block->step_event_count
    scale_axis = (((unsigned long)current_block->steps[X_AXIS])<<16) / current_block->step_event_count;
    current_motion_block->accel[X_AXIS] = (scale_axis>>7) * current_block->acceleration_steps_per_s2;
    current_motion_block->accel[X_AXIS] = (current_motion_block->accel[X_AXIS]>>16);


    // Keep acceleration high to be able to brake
    if(current_motion_block->accel[X_AXIS] == 0)
      current_motion_block->accel[X_AXIS] = 1000;

    //initial_speed[X_AXIS] = current_block->steps[X_AXIS] * current_block->initial_rate / current_block->step_event_count;
    current_motion_block->initial_speed[X_AXIS] = scale_axis * current_block->initial_rate;
    current_motion_block->initial_speed[X_AXIS] = (current_motion_block->initial_speed[X_AXIS] >> 16);

    //nominal_speed[X_AXIS] = current_block->steps[X_AXIS] * current_block->nominal_rate / current_block->step_event_count;
    current_motion_block->nominal_speed[X_AXIS] = scale_axis * current_block->nominal_rate;
    current_motion_block->nominal_speed[X_AXIS] = (current_motion_block->nominal_speed[X_AXIS]>>16);

    if(current_motion_block->nominal_speed[X_AXIS] < current_motion_block->initial_speed[X_AXIS])
      current_motion_block->initial_speed[X_AXIS] = current_motion_block->nominal_speed[X_AXIS];

    //final_speed[X_AXIS] = current_block->steps[X_AXIS] * current_block->final_rate / current_block->step_event_count;
    current_motion_block->final_speed[X_AXIS] = scale_axis * current_block->final_rate;
    current_motion_block->final_speed[X_AXIS] = (current_motion_block->final_speed[X_AXIS]>>16);

    if(current_motion_block->final_speed[X_AXIS] < 10)
      current_motion_block->final_speed[X_AXIS] = 10;

    //speed_x = - speed_x;
    if((out_bits & (1<<X_AXIS))!=0) {
      pos[X_AXIS] -= current_block->steps[X_AXIS];
      current_motion_block->pos[X_AXIS] = pos[X_AXIS];
    } else {
      pos[X_AXIS] += current_block->steps[X_AXIS];
      current_motion_block->pos[X_AXIS] = pos[X_AXIS];
    }

    //store the calculate values in motion_buffer_block_old
    motion_buffer_block_old.accel[X_AXIS] = current_motion_block->accel[X_AXIS];
    motion_buffer_block_old.initial_speed[X_AXIS] = current_motion_block->initial_speed[X_AXIS];
    motion_buffer_block_old.nominal_speed[X_AXIS] = current_motion_block->nominal_speed[X_AXIS];
    motion_buffer_block_old.final_speed[X_AXIS] = current_motion_block->final_speed[X_AXIS];
    motion_buffer_block_old.pos[X_AXIS] = current_motion_block->pos[X_AXIS];
  } else {
    //If there is no movement in the x-axis use the last calculate values
    current_motion_block->accel[X_AXIS] = motion_buffer_block_old.accel[X_AXIS];
    current_motion_block->initial_speed[X_AXIS] = motion_buffer_block_old.initial_speed[X_AXIS];
    current_motion_block->nominal_speed[X_AXIS] = motion_buffer_block_old.nominal_speed[X_AXIS];
    current_motion_block->final_speed[X_AXIS] = motion_buffer_block_old.final_speed[X_AXIS];
    current_motion_block->pos[X_AXIS] = motion_buffer_block_old.pos[X_AXIS];
  }


  // If there is any movement in the y-axis
  if(current_block->steps[Y_AXIS] != 0) {
    //accel[Y_AXIS] = current_block->steps[Y_AXIS] * current_block->acceleration_steps_per_s2 / current_block->step_event_count;
    scale_axis = (((unsigned long)current_block->steps[Y_AXIS])<<16) / current_block->step_event_count;
    current_motion_block->accel[Y_AXIS] = (scale_axis>>7) * current_block->acceleration_steps_per_s2;
    current_motion_block->accel[Y_AXIS] = (current_motion_block->accel[Y_AXIS]>>16);


    // Keep acceleration high to be able to brake
    if(current_motion_block->accel[Y_AXIS] == 0)
      current_motion_block->accel[Y_AXIS] = 1000;

    //initial_speed[Y_AXIS] = current_block->steps[Y_AXIS] * current_block->initial_rate / current_block->step_event_count;
    current_motion_block->initial_speed[Y_AXIS] = scale_axis * current_block->initial_rate;
    current_motion_block->initial_speed[Y_AXIS] = (current_motion_block->initial_speed[Y_AXIS]>>16);

    //nominal_speed[Y_AXIS] = current_block->steps[Y_AXIS] * current_block->nominal_rate / current_block->step_event_count;
    current_motion_block->nominal_speed[Y_AXIS] = scale_axis * current_block->nominal_rate;
    current_motion_block->nominal_speed[Y_AXIS] = (current_motion_block->nominal_speed[Y_AXIS]>>16);

    if(current_motion_block->nominal_speed[Y_AXIS] < current_motion_block->initial_speed[Y_AXIS])
      current_motion_block->initial_speed[Y_AXIS] = current_motion_block->nominal_speed[Y_AXIS];

    //final_speed[Y_AXIS] = current_block->steps[Y_AXIS] * current_block->final_rate / current_block->step_event_count;
    current_motion_block->final_speed[Y_AXIS] = scale_axis * current_block->final_rate;
    current_motion_block->final_speed[Y_AXIS] = (current_motion_block->final_speed[Y_AXIS]>>16);

    if(current_motion_block->final_speed[Y_AXIS] < 10)
      current_motion_block->final_speed[Y_AXIS] = 10;

    //speed_y = - speed_y;
    if((out_bits & (1<<Y_AXIS))!=0) {
      pos[Y_AXIS] -= current_block->steps[Y_AXIS];
      current_motion_block->pos[Y_AXIS] = pos[Y_AXIS];
    } else {
      pos[Y_AXIS] += current_block->steps[Y_AXIS];
      current_motion_block->pos[Y_AXIS] = pos[Y_AXIS];
    }

    //store the calculate values in motion_buffer_block_old
    motion_buffer_block_old.accel[Y_AXIS] = current_motion_block->accel[Y_AXIS];
    motion_buffer_block_old.initial_speed[Y_AXIS] = current_motion_block->initial_speed[Y_AXIS];
    motion_buffer_block_old.nominal_speed[Y_AXIS] = current_motion_block->nominal_speed[Y_AXIS];
    motion_buffer_block_old.final_speed[Y_AXIS] = current_motion_block->final_speed[Y_AXIS];
    motion_buffer_block_old.pos[Y_AXIS] = current_motion_block->pos[Y_AXIS];
  } else {
    //If there is no movement in the y-axis use the last calculate values
    current_motion_block->accel[Y_AXIS] = motion_buffer_block_old.accel[Y_AXIS];
    current_motion_block->initial_speed[Y_AXIS] = motion_buffer_block_old.initial_speed[Y_AXIS];
    current_motion_block->nominal_speed[Y_AXIS] = motion_buffer_block_old.nominal_speed[Y_AXIS];
    current_motion_block->final_speed[Y_AXIS] = motion_buffer_block_old.final_speed[Y_AXIS];
    current_motion_block->pos[Y_AXIS] = motion_buffer_block_old.pos[Y_AXIS];
  }

  // If there is any movement in the z-axis
  if(current_block->steps[Z_AXIS] != 0) {
    //accel[Z_AXIS] = current_block->steps[Z_AXIS] * current_block->acceleration_steps_per_s2 / current_block->step_event_count;
    scale_axis = (((unsigned long)current_block->steps[Z_AXIS])<<16) / current_block->step_event_count;
    current_motion_block->accel[Z_AXIS] = (scale_axis>>7) * current_block->acceleration_steps_per_s2;
    current_motion_block->accel[Z_AXIS] = (current_motion_block->accel[Z_AXIS]>>16);


    // Keep acceleration high to be able to brake
    if(current_motion_block->accel[Z_AXIS] == 0)
      current_motion_block->accel[Z_AXIS] = 1000;

    //initial_speed[Z_AXIS] = current_block->steps[Z_AXIS] * current_block->initial_rate / current_block->step_event_count;
    current_motion_block->initial_speed[Z_AXIS] = scale_axis * current_block->initial_rate;
    current_motion_block->initial_speed[Z_AXIS] = (current_motion_block->initial_speed[Z_AXIS]>>16);

    //nominal_speed[Z_AXIS] = current_block->steps[Z_AXIS] * current_block->nominal_rate / current_block->step_event_count;
    current_motion_block->nominal_speed[Z_AXIS] = scale_axis * current_block->nominal_rate;
    current_motion_block->nominal_speed[Z_AXIS] = (current_motion_block->nominal_speed[Z_AXIS]>>16);

    if(current_motion_block->nominal_speed[Z_AXIS] < current_motion_block->initial_speed[Z_AXIS])
      current_motion_block->initial_speed[Z_AXIS] = current_motion_block->nominal_speed[Z_AXIS];

    //final_speed[Z_AXIS] = current_block->steps[Z_AXIS] * current_block->final_rate / current_block->step_event_count;
    current_motion_block->final_speed[Z_AXIS] = scale_axis * current_block->final_rate;
    current_motion_block->final_speed[Z_AXIS] = (current_motion_block->final_speed[Z_AXIS]>>16);

    if(current_motion_block->final_speed[Z_AXIS] < 10)
      current_motion_block->final_speed[Z_AXIS] = 10;
    if(current_motion_block->final_speed[Z_AXIS] > 800)
      current_motion_block->final_speed[Z_AXIS] = 800;

    //speed_z = - speed_z;
    if((out_bits & (1<<Z_AXIS))!=0) {
      pos[Z_AXIS] -= current_block->steps[Z_AXIS];
      current_motion_block->pos[Z_AXIS] = pos[Z_AXIS];
    } else {
      pos[Z_AXIS] += current_block->steps[Z_AXIS];
      current_motion_block->pos[Z_AXIS] = pos[Z_AXIS];
    }

    current_motion_block->pos_change_z = true;
  } else {
    current_motion_block->pos_change_z = false;
  }

  // If there is any movement in the e-axis
  if(current_block->steps[E_AXIS] != 0) {
    //accel[E_AXIS] = current_block->steps[E_AXIS] * current_block->acceleration_steps_per_s2 / current_block->step_event_count;
    scale_axis = (((unsigned long)current_block->steps[E_AXIS])<<16) / current_block->step_event_count;
    current_motion_block->accel[E_AXIS] = (scale_axis>>7) * current_block->acceleration_steps_per_s2;
    current_motion_block->accel[E_AXIS] = (current_motion_block->accel[E_AXIS]>>16);

    // Keep acceleration high to be able to brake
    if(current_motion_block->accel[E_AXIS] == 0)
      current_motion_block->accel[E_AXIS] = 1000;

    //initial_speed[E_AXIS] = current_block->steps[E_AXIS] * current_block->initial_rate / current_block->step_event_count;
    current_motion_block->initial_speed[E_AXIS] = scale_axis * current_block->initial_rate;
    current_motion_block->initial_speed[E_AXIS] = (current_motion_block->initial_speed[E_AXIS]>>16);

    //nominal_speed[E_AXIS] = current_block->steps[E_AXIS] * current_block->nominal_rate / current_block->step_event_count;
    current_motion_block->nominal_speed[E_AXIS] = scale_axis * current_block->nominal_rate;
    current_motion_block->nominal_speed[E_AXIS] = (current_motion_block->nominal_speed[E_AXIS]>>16);

    if(current_motion_block->nominal_speed[E_AXIS] < current_motion_block->initial_speed[E_AXIS])
      current_motion_block->initial_speed[E_AXIS] = current_motion_block->nominal_speed[E_AXIS];

    //final_speed[E_AXIS] = current_block->steps[E_AXIS] * current_block->final_rate / current_block->step_event_count;
    current_motion_block->final_speed[E_AXIS] = scale_axis * current_block->final_rate;
    current_motion_block->final_speed[E_AXIS] = (current_motion_block->final_speed[E_AXIS]>>16);

    if(current_motion_block->final_speed[E_AXIS] < 10)
      current_motion_block->final_speed[E_AXIS] = 10;
    if(current_motion_block->final_speed[E_AXIS] > 800)
      current_motion_block->final_speed[E_AXIS] = 800;

    //speed_e = - speed_e;
    if((out_bits & (1<<E_AXIS))!=0) {
      pos[E_AXIS] -= current_block->steps[E_AXIS];
      current_motion_block->pos[E_AXIS] = pos[E_AXIS];
    } else {
      pos[E_AXIS] += current_block->steps[E_AXIS];
      current_motion_block->pos[E_AXIS] = pos[E_AXIS];
    }

    //store the calculate values in motion_buffer_block_old
    motion_buffer_block_old.accel[E_AXIS] = current_motion_block->accel[E_AXIS];
    motion_buffer_block_old.initial_speed[E_AXIS] = current_motion_block->initial_speed[E_AXIS];
    motion_buffer_block_old.nominal_speed[E_AXIS] = current_motion_block->nominal_speed[E_AXIS];
    motion_buffer_block_old.final_speed[E_AXIS] = current_motion_block->final_speed[E_AXIS];
    motion_buffer_block_old.pos[E_AXIS] = current_motion_block->pos[E_AXIS];
  } else {
    //If there is no movement in the e-axis use the last calculate values
    current_motion_block->accel[E_AXIS] = motion_buffer_block_old.accel[E_AXIS];
    current_motion_block->initial_speed[E_AXIS] = motion_buffer_block_old.initial_speed[E_AXIS];
    current_motion_block->nominal_speed[E_AXIS] = motion_buffer_block_old.nominal_speed[E_AXIS];
    current_motion_block->final_speed[E_AXIS] = motion_buffer_block_old.final_speed[E_AXIS];
    current_motion_block->pos[E_AXIS] = motion_buffer_block_old.pos[E_AXIS];
  }

  // Calculate duration of the movement
  // I - Acceleration phase
  double temp;
  //timerClk = (current_block->nominal_rate - current_block->initial_rate) / current_block->acceleration_steps_per_s2;
  nextTimerClk = current_block->nominal_rate - current_block->initial_rate;
  temp = (double) nextTimerClk /  current_block->acceleration_steps_per_s2;
  nextTimerClk = temp * 2000000;

  // II - Plateau / Constant speed phase (if applies)
  if (current_block->decelerate_after > current_block->accelerate_until) {
    //timer += phase2duration;
    //timerClk = timerClk + (2000 *1000 * (current_block->decelerate_after - current_block->accelerate_until) / current_block->nominal_rate);
    TEMPtimerClk = current_block->decelerate_after - current_block->accelerate_until;
    TEMPtimerClk = TEMPtimerClk / current_block->nominal_rate;
    TEMPtimerClk = TEMPtimerClk * 2000000;
    nextTimerClk = nextTimerClk + TEMPtimerClk;
  }

  // III - Deceleration phase
  //timerClk = 2000 * 1000 * (current_block->nominal_rate - current_block->final_rate) / current_block->acceleration_steps_per_s2;
  TEMPtimerClk = current_block->nominal_rate - current_block->final_rate;
  TEMPtimerClk = TEMPtimerClk / current_block->acceleration_steps_per_s2;
  TEMPtimerClk = TEMPtimerClk * 2000000;
  TEMPtimerClk = nextTimerClk + TEMPtimerClk;

  // Adjust value for the internal units of the chip (we changed units of the acceleration, but speed has also internal units)
  nextTimerClk = TEMPtimerClk * TMC5130_t_factor;

  current_motion_block->nextTimerClk = nextTimerClk;
  motion_buffer_block_old.nextTimerClk = current_motion_block->nextTimerClk;

  // Calculations finished: Let the interrupt send the information
  current_motion_block->calcready = true;
  motion_buffer_block_old.calcready = current_motion_block->calcready;
  append_motion_block();
  current_block = NULL;
  planner.discard_current_block();  
}

/**
 * @brief "The Stepper Driver Interrupt" - This timer interrupt is the workhorse.
 * It has been divided into st_calculate and this function. In this part we send the information of the current
 * block, once the calculations of st_calculate are ready (current_block->ready) and set the timer for the next
 * wake-up
 * @return none
 */
void Trams::isr() {
  unsigned long temp_timer;
  motion_block_t *current_motion_block;

  if (timerClk == 0) {
    current_motion_block =  get_current_motion_block();
    if ((current_motion_block->calcready == true) && (current_motion_block != NULL)) {
      // Send accel and nominal speed SPI datagrams
      spi_writeRegister(VMAX, current_motion_block->nominal_speed[X_AXIS],  X_AXIS);  //Velocity of X
      spi_writeRegister(AMAX, current_motion_block->accel[X_AXIS],          X_AXIS);  //ACC of X
      spi_writeRegister(DMAX, current_motion_block->accel[X_AXIS],          X_AXIS);  //DEC of X
      spi_writeRegister(VMAX, current_motion_block->nominal_speed[Y_AXIS],  Y_AXIS);  //Velocity of Y
      spi_writeRegister(AMAX, current_motion_block->accel[Y_AXIS],          Y_AXIS);  //ACC of Y
      spi_writeRegister(DMAX, current_motion_block->accel[Y_AXIS],          Y_AXIS);  //DEC of Y

      // only send if there is any movement in the z-axis
      if (current_motion_block->pos_change_z == true) {
        spi_writeRegister(VMAX, current_motion_block->nominal_speed[Z_AXIS],  Z_AXIS); //Velocity of ZAXIS
        spi_writeRegister(AMAX, current_motion_block->accel[Z_AXIS],          Z_AXIS); //ACC of ZAXIS
        spi_writeRegister(DMAX, current_motion_block->accel[Z_AXIS],          Z_AXIS); //DEC of ZAXIS
      }

      //check if new data receive
      //st_check_UART_rx();

      spi_writeRegister(VMAX, current_motion_block->nominal_speed[E_AXIS],  E_AXIS);  //Velocity of E0AXIS
      spi_writeRegister(AMAX, current_motion_block->accel[E_AXIS],          E_AXIS);  //ACC of E0AXIS
      spi_writeRegister(DMAX, current_motion_block->accel[E_AXIS],          E_AXIS);  //DEC of E0AXIS

      // Send initial and final speeed SPI datagrams
      spi_writeRegister(VSTART, current_motion_block->initial_speed[X_AXIS],  X_AXIS); //Initial vel of X
      spi_writeRegister(VSTOP, current_motion_block->final_speed[X_AXIS],     X_AXIS); //Final vel of X
      spi_writeRegister(VSTART, current_motion_block->initial_speed[Y_AXIS],  Y_AXIS); //Initial vel of Y
      spi_writeRegister(VSTOP, current_motion_block->final_speed[Y_AXIS],     Y_AXIS); //Final vel of Y

      // only send if there is any movement in the z-axis
      if (current_motion_block->pos_change_z == true) {
        spi_writeRegister(VSTART, current_motion_block->initial_speed[Z_AXIS],  Z_AXIS);//Initial vel of Z
        spi_writeRegister(VSTOP, current_motion_block->final_speed[Z_AXIS],     Z_AXIS);//Final vel of Z
      }

      //check if new data receive
      //st_check_UART_rx();

      spi_writeRegister(VSTART, current_motion_block->initial_speed[E_AXIS],  E_AXIS); //Initial vel of E0
      spi_writeRegister(VSTOP, current_motion_block->final_speed[E_AXIS],     E_AXIS);    //Final vel of E0

      // Send target positions, movement starts immediately
      // only send if there is any movement in the z-axis
      if (current_motion_block->pos_change_z == true)
        spi_writeRegister(XTARGET, current_motion_block->pos[Z_AXIS], Z_AXIS);   // target

      spi_writeRegister(XTARGET, current_motion_block->pos[X_AXIS],   X_AXIS);   // target
      spi_writeRegister(XTARGET, current_motion_block->pos[Y_AXIS],   Y_AXIS);   // target
      spi_writeRegister(XTARGET, current_motion_block->pos[E_AXIS],   E_AXIS);  // target
      
      // Let's wake up back when this movement is over
      timerClk = current_motion_block->nextTimerClk;

      // Discard the current_block and let st_calculate start with the next one
      current_motion_block->calcready = false;
      discard_current_motion_block();
    } else {
      timerClk = 2000; // Check in a short time, if a new block is calculated
    }
  }

  // Update 32-bit software-extended timer
  if(timerClk >= 65535) {
    timerClk = timerClk - 65535;
    TCNT1 = 1;
  } else {
    temp_timer = TCNT1;
    if(timerClk > (temp_timer + 100))
      TCNT1 = 65535 - timerClk + temp_timer;
    else
      TCNT1 = 65436;

    timerClk = 0;
  }
}

/**
 * @brief Initialize the Trinamic Drivers(TMC5130)
 * @param	csPin				chip select for spi (XAXIS,YAXIS,ZAXIS,E0AXIS)
 * @param	irun				Motor run current (0..31)
 * @param	ihold				Standstill current (0..31)
 * @param	stepper_direction	inverse/ not inverse
 * @return	none
 */
void Trams::TMC5130_init(const AxisEnum &axis, uint8_t irun, uint8_t ihold, uint8_t stepper_direction, uint16_t sw_register) {
	uint32_t value;
	value = SET_IHOLD(ihold) | SET_IRUN(irun) | SET_IHOLDDELAY(7);
	spi_writeRegister(IHOLD_IRUN, value, axis);		//IHOLD and IRUN current
	spi_writeRegister(RAMPMODE, 0x0, axis);			//select position mode
	spi_writeRegister(V_1, 0x0, axis);					//Disables A1 and D1 in position mode, amax and vmax only
	spi_writeRegister(D_1, 0x10, axis);				//D1 not zero
	spi_writeRegister(AMAX, 0xFFFF, axis);				//Acceleration
	spi_writeRegister(VMAX, 0xFFFF, axis);				//Velocity
	spi_writeRegister(CHOPCONF, 0x140101D5, axis);		//Chopper Configuration
	spi_writeRegister(GCONF, 0x1084 | stepper_direction, axis);	//General Configuration
	spi_writeRegister(SW_MODE, sw_register, axis);
}

void Trams::init() {
  spi_init();
  TMC5130_init( X_AXIS,  X_CURRENT_RUN,  X_CURRENT_HOLD,  STEPPER_DIRECTION_X,  SWITCH_POSITION_X | SWITCH_POLARITY_X);
  TMC5130_init( Y_AXIS,  Y_CURRENT_RUN,  Y_CURRENT_HOLD,  STEPPER_DIRECTION_Y,  SWITCH_POSITION_Y | SWITCH_POLARITY_Y);
  TMC5130_init( Z_AXIS,  Z_CURRENT_RUN,  Z_CURRENT_HOLD,  STEPPER_DIRECTION_Z,  SWITCH_POSITION_Z | SWITCH_POLARITY_Z);
  TMC5130_init(E_AXIS, E0_CURRENT_RUN, E0_CURRENT_HOLD,  STEPPER_DIRECTION_E0, false);

  disable_all_steppers();

  // Init Enable Pins - steppers default to disabled.
  #if HAS_X_ENABLE
    X_ENABLE_INIT;
    if (!X_ENABLE_ON) X_ENABLE_WRITE(HIGH);
  #endif
  #if HAS_Y_ENABLE
    Y_ENABLE_INIT;
    if (!Y_ENABLE_ON) Y_ENABLE_WRITE(HIGH);
  #endif
  #if HAS_Z_ENABLE
    Z_ENABLE_INIT;
    if (!Z_ENABLE_ON) Z_ENABLE_WRITE(HIGH);
  #endif
  #if HAS_E0_ENABLE
    E0_ENABLE_INIT;
    if (!E_ENABLE_ON) E0_ENABLE_WRITE(HIGH);
  #endif

  // Init endstops and pullups
  endstops.init();

  // waveform generation = 0100 = CTC
  SET_WGM(1, CTC_OCRnA);

  // output mode = 00 (disconnected)
  SET_COMA(1, NORMAL);

  // Set the timer pre-scaler
  // Generally we use a divider of 8, resulting in a 2MHz timer
  // frequency on a 16MHz MCU. If you are going to change this, be
  // sure to regenerate speed_lookuptable.h with
  // create_speed_lookuptable.py
  SET_CS(1, PRESCALER_8);  //  CS 2 = 1/8 prescaler

  // Init Stepper ISR to 122 Hz for quick starting
  OCR1A = 0x4000;
  TCNT1 = 0;
  ENABLE_STEPPER_DRIVER_INTERRUPT();

  #if ENABLED(ADVANCE) || ENABLED(LIN_ADVANCE)
    for (uint8_t i = 0; i < COUNT(e_steps); i++) e_steps[i] = 0;
    #if ENABLED(LIN_ADVANCE)
      ZERO(current_adv_steps);
    #endif
  #endif // ADVANCE || LIN_ADVANCE

  endstops.enable(true); // Start with endstops active. After homing they can be disabled
  sei();

  set_directions(); // Init directions to last_direction_bits = 0
}

void Trams::set_position(const long &a, const long &b, const long &c, const long &e) {

  synchronize(); // Bad to set stepper counts in the middle of a move

  CRITICAL_SECTION_START;

  if(pos[X_AXIS] != a) {
    count_position[X_AXIS] = pos[X_AXIS] = a;
    motion_buffer_block_old.pos[X_AXIS] = a;
    // Update position in the driver
    spi_writeRegister(RAMPMODE, HOLD_MODE, X_AXIS);  //HOLD Mode
    spi_writeRegister(XTARGET, a, X_AXIS);
    spi_writeRegister(XACTUAL, a, X_AXIS);
    spi_writeRegister(RAMPMODE, POSITIONING_MODE, X_AXIS); //Position MODE
  }
  if(pos[Y_AXIS] != b) {
    count_position[Y_AXIS] = pos[Y_AXIS] = b;
    motion_buffer_block_old.pos[Y_AXIS] = b;
    // Update position in the driver
    spi_writeRegister(RAMPMODE, HOLD_MODE, Y_AXIS);  //HOLD Mode
    spi_writeRegister(XTARGET, b, Y_AXIS);
    spi_writeRegister(XACTUAL, b, Y_AXIS);
    spi_writeRegister(RAMPMODE, POSITIONING_MODE, Y_AXIS); //Position MODE
  }
  if(pos[Z_AXIS] != c) {
    count_position[Z_AXIS] = pos[Z_AXIS] = c;
    motion_buffer_block_old.pos[Z_AXIS] = c;
    // Update position in the driver
    spi_writeRegister(RAMPMODE, HOLD_MODE, Z_AXIS);  //HOLD Mode
    spi_writeRegister(XTARGET, c, Z_AXIS);
    spi_writeRegister(XACTUAL, c, Z_AXIS);
    spi_writeRegister(RAMPMODE, POSITIONING_MODE, Z_AXIS); //Position MODE
  }
  if(pos[E_AXIS] != e) {
    count_position[E_AXIS] = pos[E_AXIS] = e;
    motion_buffer_block_old.pos[E_AXIS] = e;
    // Update position in the driver
    spi_writeRegister(RAMPMODE, HOLD_MODE, E_AXIS); //HOLD Mode
    spi_writeRegister(XTARGET, e, E_AXIS);
    spi_writeRegister(XACTUAL, e, E_AXIS);
    spi_writeRegister(RAMPMODE, POSITIONING_MODE, E_AXIS);  //Position MODE
  }

  CRITICAL_SECTION_END;
}

void Trams::set_position(const AxisEnum &axis, const long &v) {
  CRITICAL_SECTION_START;
  count_position[axis] = v;
  if(pos[axis] != v) {
    count_position[axis] = pos[axis] = v;
    motion_buffer_block_old.pos[axis] = v;
    // Update position in the driver
    spi_writeRegister(RAMPMODE, HOLD_MODE, axis); //HOLD Mode
    spi_writeRegister(XTARGET, v, axis);
    spi_writeRegister(XACTUAL, v, axis);
    spi_writeRegister(RAMPMODE, POSITIONING_MODE, axis);  //Position MODE
  }
  CRITICAL_SECTION_END;
}

void Trams::set_e_position(const long &e) {
  CRITICAL_SECTION_START;
  count_position[E_AXIS] = e;
  if(pos[E_AXIS] != e) {
    count_position[E_AXIS] = pos[E_AXIS] = e;
    motion_buffer_block_old.pos[E_AXIS] = e;
    // Update position in the driver
    spi_writeRegister(RAMPMODE, HOLD_MODE, E_AXIS); //HOLD Mode
    spi_writeRegister(XTARGET, e, E_AXIS);
    spi_writeRegister(XACTUAL, e, E_AXIS);
    spi_writeRegister(RAMPMODE, POSITIONING_MODE, E_AXIS);  //Position MODE
  }
  CRITICAL_SECTION_END;
}

void Trams::set_directions() {

  #define SET_STEP_DIR(AXIS) \
    if (motor_direction(AXIS ##_AXIS)) { \
      AXIS ##_APPLY_DIR(INVERT_## AXIS ##_DIR, false); \
      count_direction[AXIS ##_AXIS] = -1; \
    } \
    else { \
      AXIS ##_APPLY_DIR(!INVERT_## AXIS ##_DIR, false); \
      count_direction[AXIS ##_AXIS] = 1; \
    }

  #if HAS_X_DIR
    SET_STEP_DIR(X); // A
  #endif
  #if HAS_Y_DIR
    SET_STEP_DIR(Y); // B
  #endif
  #if HAS_Z_DIR
    SET_STEP_DIR(Z); // C
  #endif

  #if DISABLED(ADVANCE) && DISABLED(LIN_ADVANCE)
    if (motor_direction(E_AXIS)) {
      //REV_E_DIR();
      count_direction[E_AXIS] = -1;
    }
    else {
      //NORM_E_DIR();
      count_direction[E_AXIS] = 1;
    }
  #endif // !ADVANCE && !LIN_ADVANCE
}

// TRAMS endstops
void TramsEndstops::M119() {
  SERIAL_PROTOCOLLNPGM(MSG_M119_REPORT);
  #if ENABLED(USE_XMIN_PLUG)
    SERIAL_PROTOCOLPGM(MSG_X_MIN);
    SERIAL_PROTOCOLLN( ((spi_readRegister(RAMP_STAT, X_AXIS)&STATUS_STOP_L_bm)>>STATUS_STOP_L_bp) ? MSG_ENDSTOP_HIT : MSG_ENDSTOP_OPEN );
  #endif
  #if ENABLED(USE_XMAX_PLUG)
    SERIAL_PROTOCOLPGM(MSG_X_MAX);
    SERIAL_PROTOCOLLN( ((spi_readRegister(RAMP_STAT, X_AXIS)&STATUS_STOP_R_bm)>>STATUS_STOP_R_bp) ? MSG_ENDSTOP_HIT : MSG_ENDSTOP_OPEN );
  #endif
  #if ENABLED(USE_YMIN_PLUG)
    SERIAL_PROTOCOLPGM(MSG_Y_MIN);
    SERIAL_PROTOCOLLN( ((spi_readRegister(RAMP_STAT, Y_AXIS)&STATUS_STOP_L_bm)>>STATUS_STOP_L_bp) ? MSG_ENDSTOP_HIT : MSG_ENDSTOP_OPEN );
  #endif
  #if ENABLED(USE_YMAX_PLUG)
    SERIAL_PROTOCOLPGM(MSG_Y_MAX);
    SERIAL_PROTOCOLLN( ((spi_readRegister(RAMP_STAT, Y_AXIS)&STATUS_STOP_R_bm)>>STATUS_STOP_R_bp) ? MSG_ENDSTOP_HIT : MSG_ENDSTOP_OPEN );
  #endif
  #if ENABLED(USE_ZMIN_PLUG)
    SERIAL_PROTOCOLPGM(MSG_Z_MIN);
    SERIAL_PROTOCOLLN( ((spi_readRegister(RAMP_STAT, Z_AXIS)&STATUS_STOP_L_bm)>>STATUS_STOP_L_bp) ? MSG_ENDSTOP_HIT : MSG_ENDSTOP_OPEN );
  #endif
  #if ENABLED(USE_ZMAX_PLUG)
    SERIAL_PROTOCOLPGM(MSG_Z_MAX);
    SERIAL_PROTOCOLLN( ((spi_readRegister(RAMP_STAT, Z_AXIS)&STATUS_STOP_R_bm)>>STATUS_STOP_R_bp) ? MSG_ENDSTOP_HIT : MSG_ENDSTOP_OPEN );
  #endif
  #if ENABLED(Z_MIN_PROBE_ENDSTOP)
    SERIAL_PROTOCOLPGM(MSG_Z_PROBE);
    SERIAL_PROTOCOLLN(((READ(Z_MIN_PROBE_PIN)^Z_MIN_PROBE_ENDSTOP_INVERTING) ? MSG_ENDSTOP_HIT : MSG_ENDSTOP_OPEN));
  #endif
  #if ENABLED(FILAMENT_RUNOUT_SENSOR)
    SERIAL_PROTOCOLPGM(MSG_FILAMENT_RUNOUT_SENSOR);
    SERIAL_PROTOCOLLN(((READ(FIL_RUNOUT_PIN)^FIL_RUNOUT_INVERTING) ? MSG_ENDSTOP_HIT : MSG_ENDSTOP_OPEN));
  #endif
}