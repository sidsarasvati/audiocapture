#ifndef SIGPROC_CHEBY1_H
#define SIGPROC_CHEBY1_H

double * newCheby1Array(int NumPoles, int * size);

int cheby1(int NumPoles,int PercentRipple,double Fc,int createHighPass, double * B, double * A, int size);
void cheby1sub(int P, int NP, int PR, int LH, double FC, double *TB, double *TA);

#endif