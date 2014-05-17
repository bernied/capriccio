/* 
 * Copyright (c) 2001 by Matt Welsh and The Regents of the University of 
 * California. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without written agreement is
 * hereby granted, provided that the above copyright notice and the following
 * two paragraphs appear in all copies of this software.
 * 
 * IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING OUT
 * OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF
 * CALIFORNIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 * Author: Matt Welsh <mdw@cs.berkeley.edu>
 *
 * Ported from java code in seda/apps/Haboob/client/HttpLoad.java by Rob von Behren
 * 
 */


#include <stdlib.h>

// Number of classes
//static int NUMCLASSES = 4;

// Number of directories - based on load value
static int NUMDIRS;

// Number of files
static int NUMFILES = 8;

// Zipf distribution table for directory
static double *DIR_ZIPF;

// Zipf distribution table for file
static double *FILE_ZIPF;

// Frequency of each class
static double CLASS_FREQ[] = { 0.35, 0.50, 0.14, 0.01 };

// Order of file popularity within each class
static int FILE_ORDER[] = { 4, 3, 5, 2, 6, 1, 7, 8, 0 };


// Setup table of Zipf distribution values according to given size
static double* setupZipf(int size) {

  double *table = malloc( sizeof(double) * (size+1) );
  double zipf_sum;
  int i;
  
  for (i = 1; i <= size; i++) {
    table[i] = (double)1.0 / (double)i;
  }
  
  zipf_sum = 0.0;
  for (i = 1; i <= size; i++) {
    zipf_sum += table[i];
    table[i] = zipf_sum;
  }
  table[size] = 0.0;
  table[0] = 0.0;
  for (i = 0; i < size; i++) {
    table[i] = 1.0 - (table[i] / zipf_sum);
  }
  return table;
}


// Set up distribution tables
void setupDists(int LOAD_CONNECTIONS) {

  // Compute number of directories according to SPECweb99 rules
  {
    double opsps = (400000.0 / 122000.0) * LOAD_CONNECTIONS;
    NUMDIRS = (int)(25 + (opsps/5));
    DIR_ZIPF = setupZipf(NUMDIRS);
    FILE_ZIPF = setupZipf(NUMFILES);
  }

  // Sum up CLASS_FREQ table 
  {
    unsigned int i;
    for (i = 1; i < sizeof(CLASS_FREQ)/sizeof(CLASS_FREQ[0]); i++) {
      CLASS_FREQ[i] += CLASS_FREQ[i-1];
    }
  }
}


//make a random double b/w 0 and 1
#define RANDOM_DOUBLE ( (float) random() / (float) 0x7fffffff )

// Return index into Zipf table of random number chosen from 0.0 to 1.0
static int zipf(double table[]) {
  double r = RANDOM_DOUBLE;

  int i = 0;
  while (r < table[i]) {
    i++;
  }
  return i-1;
}


int spec_dir() {
  return zipf(DIR_ZIPF);
}

int spec_file() {
  return FILE_ORDER[ zipf(FILE_ZIPF) ];
}

int spec_class() {
  double d = RANDOM_DOUBLE;
  int class = 0;
  while (d > CLASS_FREQ[class]) class++;
  return class;
}

