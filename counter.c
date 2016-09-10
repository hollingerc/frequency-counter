/*
 * counter.c
 *
 * Created: 2014-12-22
 *  Author: Craig Hollinger
 *
 * This is very simple demonstration code that implements a simple frequency 
 * counter.  The frequency to be counted is applied to the T1 pin (pin PORTD5)
 * which is the counter input to Timer/Counter 1.  The TCC1 register is used to
 * accumulate a count of the pulses applied to the input.  The register is not
 * big enough to count all the pulses for higher frequencies, so another 
 * register is setup in RAM to count the number of TCC1 overflows.
 *
 * Timer/Counter 2 is setup to give a one second count.  During this one second
 * period, TCC1 and the associated RAM register accumulates counts of the
 * frequency applied to the T1 pin.  When the one second period expires, TCC1 is
 * turned off and the accumulated count in TCC1 and the RAM register will be the
 * number of pulses per second or the frequency of the applied signal.
 *
 * This value is converted to a string and sent to the LCD and the whole process
 * repeats indefinitely.
 *
 * Note:
 * This code won't run under Arduino for varios reasons:
 *    - Arduino utilizes TCC1 in the background
 *    - this code assumes a main processor clock frequency of 20MHz not the 16MHz
 *      of Arduino
 *    - the method main() is defined here, whereas Arduino defines it elsewhere
 *      so the compiler will flag the conflict as an error
 *
 * This file is free software; you can redistribute it and/or modify it under
 * the terms of either the GNU General Public License version 3 or the GNU
 * Lesser General Public License version 3, both as published by the Free
 * Software Foundation.
 */
#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdlib.h>
#include <util/delay.h>
/* LCD driver, you can use your own here */
#include "lcd/hd44780.h"

/* This union allows access to the individual bits of a byte in RAM. Each bit
 * is used as a flag.
 */
typedef union _FLAGS_TYPE
{
  struct{
    unsigned dataReady :1;/* new frequency data is available to be processed */
    unsigned lastSec :1;/* last overflow of TCC2 for a one second count */
    unsigned bit2 :1;
    unsigned bit3 :1;
    unsigned bit4 :1;
    unsigned bit5 :1;
    unsigned bit6 :1;
    unsigned bit7 :1;
  };
  char Val;
} FLAGS_TYPE;

/* Variables  */

/* This variable contains flags used to signal different processes in the code.
 */
volatile FLAGS_TYPE flags;

/* This variable counts Timer 2 overflows to get a one second delay. */
volatile unsigned char secTimer;

/* This variable counts Timer 1 overflows and makes up part of the resultant
   frequency count. */
volatile unsigned char freqCounter;

/* This defines the number of Timer 2 roll-overs in one second.
 *
 * The timer is clocked from the 1024 tap of the prescaler which gives one tick
 * every 51.2us (20MHz processor clock frequency).  There are 19531 51.2us tick
 * in a second.  256 ticks will produce a roll-over.
 *
 *  = 76.29 roll-overs in one second
 *
 * The second define is for the 0.29 portion of a roll-over.  This value is
 * forced into the timer register for the last portion of the one second gate.
 */
#define ONE_SEC_GATE (10000000UL / 512 / 256) // = 76
#define LAST_SEC (256 - 29 * 256 / 100) // = 256 - 74 = 182

/* t1_init()
 *
 * Setup Timer 1:
 * - increment on clock input,
 * - off,
 * - clear the counter,
 * - enable the interrupt.
 *
 * When eventually turned on it will increment from the external T1 pin.  this
 * is where the frequency to be measured is connected.
 */
void t1_init(void){
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;
  TIFR1 = _BV(TOV1);
  TIMSK1 = _BV(TOIE1);
}

/* t2_init()
 *
 * Setup Timer 2:
 * - increment on clock input,
 * - off,
 * - clear the counter,
 * - enable the overflow interrupt.
 *
 * When eventually turned on it will increment from the 1024 prescaler tap.
 */
void t2_init(void){
  TCCR2A = 0;
  TCCR2B = 0;
  TCNT2 = 0;
  TIFR2 = _BV(TOV2);
  TIMSK2 = _BV(TOIE2);
}

/* Timers 1 & 2 on and off.
 *
 * These simple functions are made in-line so as to execute quickly.
 */
static inline void timer1Off(void) __attribute__((always_inline));
void timer1Off(void){
  TCCR1B = 0;
}
static inline void timer1On(void) __attribute__((always_inline));
void timer1On(void){
  TCCR1B = 0b00000110; // T1 pin, falling edge
}
static inline void timer2Off(void) __attribute__((always_inline));
void timer2Off(void){
  TCCR2B = 0;
}
static inline void timer2On(void) __attribute__((always_inline));
void timer2On(void){
  TCCR2B = 0b00000111; // 1024 pre-scale tap
}

/* getTimer1()
 *
 * Return the value of TCNT1.
 */
int getTimer1(void){
  return(TCNT1);
}

/* Timer 1 overflow interrupt service routine.
 *
 * Count the overflows by incrementing freqCounter.
 */
ISR(TIMER1_OVF_vect){
  freqCounter++;
}

/* Timer 2 overflow interrupt service routine.
 *
 * Count the overflows with secTimer to get one second. When the time is up,
 * stop both timers and signal that new data is ready.
 */
ISR(TIMER2_OVF_vect){
  if(flags.lastSec == 1){
    timer1Off();
    timer2Off();
    flags.dataReady = 1;
    flags.lastSec = 0;
  }
  else{
    if(--secTimer == 0){
    /* this is the last roll over of 1 second, force the partial count */
      TCNT0 = LAST_SEC;
      flags.lastSec = 1;
    }
  }  
}/* end ISR(TIMER2_OVF_vect) */

int main(void){
  /* a string to store the frequency for displaying on the LCD */
  char tempStr[10];
  
  /* use this to add total accumulated pulses to get frequency */
  unsigned long frequency;

  /* initialize the LCD, put your own code here */
  hd44780_init(&PORTC, PORTC3, &PORTB, PORTB1, &PORTB, PORTB0, 2, 20);

  secTimer = ONE_SEC_GATE;/* on second counter for TCC2 */
  freqCounter = 0;/* frequency accumulator */
  flags.Val = 0;/* clear all the flags */
  
  /* initialize the timers */
  t2_init();
  t1_init();
  /* turn them on */
  timer2On();
  timer1On();
  
  /* enable the global interrupt */
  sei();
  
  /* display a startup message on the LCD */
  hd44780_putstr("Frequency Counter");
  _delay_ms(1000);
  
  /* run endlessly in this loop */
  while(1){
    /* when a new frequency has been accumulated, this flag is set */
    if(flags.dataReady == 1){
      flags.dataReady = 0;
      
      /* combine the counts from Timer1 and freqCounter */
      frequency = (unsigned long)freqCounter * 65536UL;
      frequency += (unsigned long)getTimer1();
      
      /* convert frequency to an ASCII string and display it */
      ltoa(frequency, tempStr, 10);
      hd44780_clearLine(0);
      hd44780_putstr(tempStr);
      
      /* re-start the process */
      secTimer = ONE_SEC_GATE;
      freqCounter = 0;
      t2_init();
      t1_init();
      timer2On();
      timer1On();
    }/* end if(flags.dataReady == 1) */
  }/* end while(1) */
}/* end main() */
