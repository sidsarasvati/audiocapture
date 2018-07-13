//#! gcc -g -DDEMO_RESAMPLER -DDIAGFILT $this -o cheby1.exe && cheby1.exe

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "dsp/cheby1.h++"

#undef PI
#define PI (3.14159265358979323846)

#undef TWOPI
#define TWOPI (2.0*PI)

#undef TRUE
#define TRUE 1

#undef FALSE
#define FALSE 0

#ifdef DIAG_SIGPROC
#define diag(fmt,...) printf(fmt "\r\n", ##__VA_ARGS__)
#else
#define diag(fmt,...) ;
#endif

// returns an array big enough to be passed into cheby1 for the B,A arguments.
// the only thing you need to know is that cheby1 will return the number
// of elements written to these arrays, and that you need to free() this memory
double * newCheby1Array(int NumPoles, int * size) {
	// +1 for A[0] or B[0] and +2 for temporary space
	int s = NumPoles+3;
	double * t = (double*)malloc(sizeof(double) * (s));
	if (size != NULL) {
		if (t) {
			*size = s;
		} else {
			*size = 0;
		}
	}
	return t;
}

void cheby1sub(int P, int NP, int PR, int LH, double FC, double *TB, double *TA) {
	#define arsinh(z) ( log(z + sqrt( z*z + 1. )) )
	#define arcosh(z) ( log(z + sqrt( z*z - 1. )) )
	double theta = PI/(NP*2.);
	double RP = - cos ( theta + P * PI / NP );
	double IP =   sin ( theta + P * PI / NP );
	double ES,VX,KX;
	double T,W,M,D,K,K2;
	double X0,X1,X2,Y1,Y2;

	if (PR) {
		// chebyshev warping of circle to an ellipsis.
		ES = sqrt ( pow( (100./(100.-PR)), 2.) - 1. );
		VX = (1./NP) * arsinh(1./ES);
		KX = cosh( (1./NP) * arcosh(1./ES) );
		RP = RP * sinh(VX) / KX;
		IP = IP * cosh(VX) / KX;
		diag("ES= %.6f; VX= %.6f; KX= %.6f;",ES,VX,KX);
	}
	diag("RP= %.6f; IP= %.6f",RP,IP);

	T = 2.*tan(.5);
	W = 2.*PI*FC;
	M = RP*RP + IP*IP;
	D = 4. - 4.*RP*T + M*T*T;
	diag("T_= %.6f; W_= %.6f; M_= %.6f; D_= %.6f;",T,W,M,D);

	X0 = T*T/D;
	X1 = 2.*X0;
	X2 = X0;
	Y1 = ( 8.-2.*M*T*T)/D;
	Y2 = (-4.-4.*RP*T-M*T*T)/D;
	diag("X0= %.6f; X1= %.6f; X2= %.6f;",X0,X1,X2);
	diag("Y1= %.6f; Y2= %.6f;",Y1,Y2);

	if (LH) K = -cos(W/2.+.5)/cos(W/2.-.5);
	else    K =  sin(.5-W/2.)/sin(.5+W/2.);


	K2=K*K;
	D = 1. + Y1*K - Y2*K2;
	diag("K_= %.6f; D_= %.6f",K,D);

	TA[0] = (X0 - X1*K + X2*K2)/D;
	TA[1] = (-2*X0*K + X1 + X1*K2 - 2*X2*K)/D;
	TA[2] = (X0*K2 - X1*K + X2)/D;

	TB[1] = (2*K + Y1 + Y1*K2 - 2*Y2*K)/D;
	TB[2] = (-K2 - Y1*K + Y2) / D;

	if (LH) {
		TA[1]=-TA[1];
		TB[1]=-TB[1];
	}
	diag("A0= %.6f; A1= %.6f; A2= %.6f;",TA[0],TA[1],TA[2]);
	diag("B1= %.6f; B2= %.6f;",TB[1],TB[2]);

	return;
}

// NumPoles : even integer 2 or greater
// PercentRipple:
//		0 - butterworth filter
//      1-29 : chebyshev type 1 filter with PR percent ripple
//		30 + : domain error. undefined.
// FC : normalized center frequency where 1 means the nyquist rate.
// B,A : array which will hold the recursive filter coeffs.
//       these arrays will be used as scratch space.
// size : size of B,A arrays. minimum size is NumPoles + 3
//
// On success the function returns a positive number indicating the number
// of coefficients written to the array B and A. This number will always be
// the number of poles + 1.
// On failure this function returns a negative number
//	-1 : memory error
//  all other negative numbers are errors with the input.
//
//
// This function generates coefficients of the form:
//       b_0 + b_1x + b_2x^2....
//  H = ----------------------
//        1  + a_1x + a_2x^2....
int cheby1(int NumPoles,int PercentRipple,double Fc,int createHighPass, double * B, double * A, int size) {
	// note this code is a straight translation from fortran code
	// the book used the folloing definition of a transfer function
	//
	//       a_0 + a_1x ....
	//  H = ----------------------
	//        1  - b_1x ....
	// where as my courses and previous books flipped the position of
	// B,A and defined the bottom coefficients to be positive.
	// this reverse is done at the end of the function.

	int i,p,bsize=sizeof(double) * size;
	double *TA;
	double *TB;
	double T2A[3];
	double T2B[3];
	double SA=0,SB=0,GAIN;
	int twiddle=1;

	if (PercentRipple<0 || PercentRipple>=30) {
		diag("percent ripple %d not within range 0<=PR<30.",PercentRipple);
		return -2;
	}
	if (NumPoles%2==1||NumPoles<2) {
		diag("Number Poles must be even and >=2.");
		return -3;
	}
	if (Fc<0||Fc>1) {
		diag("Fc must be in range 0 to 1. (received %.3f)",Fc);
		return -4;
	}
	if (size < NumPoles+3) {
		diag("Output size must be at least NP+3. %d<%d.",size,NumPoles+1);
		return -5;
	}

	TA = (double*) malloc( bsize );
	TB = (double*) malloc( bsize );
	if (!TA||!TB) {
		free(TA);
		free(TB);
		return -1;
	}

	Fc /= 2.; // algorithm defines Fc on the range 0 - .5 not 0 to 1.

	for (i=0;i<size;i++)
		A[i]=B[i]=TA[i]=TB[i]=0;
	A[2] = B[2] = 1.0;


	for (p=0;p<NumPoles/2;p++) {

		cheby1sub(p,NumPoles,PercentRipple,createHighPass,Fc,T2B,T2A);

		memcpy(TA,A,bsize);
		memcpy(TB,B,bsize);

		for (i=2;i<size;i++) {
			A[i] = T2A[0]*TA[i] + T2A[1]*TA[i-1] + T2A[2]*TA[i-2];
			B[i] =        TB[i] - T2B[1]*TB[i-1] - T2B[2]*TB[i-2];
		}
	}
	B[2] = 0;
	for (i=0;i<size-2;i++) {
		A[i] =  A[i+2];
		B[i] = -B[i+2];
		if (createHighPass) {
			SA += A[i] * twiddle;
			SB += B[i] * twiddle;
			twiddle = -twiddle;
		} else {
			SA += A[i];
			SB += B[i];
		}
	}
	GAIN = SA / (1. - SB);
	diag("SA= %.6f; SB= %.6f GAIN= %.6f",SA,SB,GAIN);
	// fix the 'error' in the book. (and apply gain at the same time)
	memcpy(TA,A,bsize);
	for (i=0;i<size;i++) {
		A[i] = -B[i];
		B[i] = TA[i]/GAIN;
	}
	A[0] = 1.;
	#ifdef DIAGFILT
		for (i=0;i<NumPoles+1;i++) {
			printf("B[%d]= %.6f\r\n",i,B[i]);
		}
		for (i=0;i<NumPoles+1;i++) {
			printf("A[%d]= %.6f\r\n",i,A[i]);
		}
	#endif
	free(TA);
	free(TB);

	return NumPoles+1;
}



#ifdef DEMO_RESAMPLER

int main(void) {
	// expected output
	//RP= -0.923880; IP= 0.382683
	//T_= 1.092605; W_= 0.628318; M_= 1.000000; D_= 9.231528;
	//X0= 0.129316; X1= 0.258632; X2= 0.129316;
	//Y1= 0.607963; Y2= -0.125228;
	//K_= 0.254106; D_= 1.162573
	//A0= 0.061885; A1= 0.123770; A2= 0.061885;
	//B1= 1.048600; B2= -0.296141;
	//---
	//ES= 0.484322; VX= 0.368055; KX= 1.057802;
	//RP= -0.136179; IP= 0.933223
	//T_= 1.092605; W_= 0.628318; M_= 0.889450; D_= 5.656972;
	//X0= 0.211029; X1= 0.422058; X2= 0.211029;
	//Y1= 1.038784; Y2= -0.789584;
	//K_= -0.698508; D_= 0.659649
	//A0= 0.922920; A1= -1.845841; A2= 0.922920;
	//B1= 1.446913; B2= -0.836653;
	#define tsize 7
	double T2A[tsize];
	double T2B[tsize];
	#define FC_1 .1
	#define PR_1 0
	#define NP_1 4
	#define P_1  0
	#define LH_1 0
	//cheby1sub(P_1,NP_1,PR_1,LH_1,FC_1,T2B,T2A);
	printf("---\r\n");
	#define FC_2 .1
	#define PR_2 10
	#define NP_2 4
	#define P_2  1
	#define LH_2 1
	//cheby1sub(P_2,NP_2,PR_2,LH_2,FC_2,T2B,T2A);
	printf("---\r\n");
	cheby1(4,15,.5,1,T2B,T2A,tsize);
	printf("---\r\n");
	//cheby1(4,0,.5,0,T2B,T2A,tsize);
	return 0;
}
#endif