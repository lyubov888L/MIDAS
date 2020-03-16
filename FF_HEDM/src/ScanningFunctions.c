#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <nlopt.h>
#include <omp.h>

#define deg2rad 0.0174532925199433
#define rad2deg 57.2957795130823
#define EPS 1E-10
#define CalcNorm3(x,y,z) sqrt((x)*(x) + (y)*(y) + (z)*(z))
#define CalcNorm2(x,y) sqrt((x)*(x) + (y)*(y))
#define TestBit(A,k)  (A[(k/32)] &   (1 << (k%32)))
#define SetBit(A,k)   (A[(k/32)] |=  (1 << (k%32)))
#define ClearBit(A,k) (A[(k/32)] &= ~(1 << (k%32)))
int numProcs;

static inline double sind(double x){return sin(deg2rad*x);}
static inline double cosd(double x){return cos(deg2rad*x);}
static inline double tand(double x){return tan(deg2rad*x);}
static inline double asind(double x){return rad2deg*(asin(x));}
static inline double acosd(double x){return rad2deg*(acos(x));}
static inline double atand(double x){return rad2deg*(atan(x));}
static inline double sin_cos_to_angle (double s, double c){return (s >= 0.0) ? acos(c) : 2.0 * M_PI - acos(c);}

static void check (int test, const char * message, ...){
    if (test) {
        va_list args;
        va_start (args, message);
        vfprintf (stderr, message, args);
        va_end (args);
        fprintf (stderr, "\n");
        exit (EXIT_FAILURE);
    }
}

static inline void CalcEtaAngle(double y,double z,double *alpha) { // No return but a pointer is updated.
	*alpha = rad2deg * acos(z/sqrt(y*y+z*z));
	if (y > 0)    *alpha = -*alpha;
}

static inline double CalcEta(double y, double z) { // Returns the eta
	double alpha;
	alpha = rad2deg * acos(z/sqrt(y*y+z*z));
	if (y > 0) alpha = -alpha;
	return alpha;
}

static inline void MatrixMult(double m[3][3], double v[3], double r[3]){
	int i;
	for (i=0; i<3; i++) {
		r[i] = m[i][0]*v[0] + m[i][1]*v[1] + m[i][2]*v[2];
	}
}

static inline void CorrectHKLsLatC(double LatC[6], double *hklsIn,int nhkls,double Lsd,double Wavelength,double *hkls){
	double a=LatC[0],b=LatC[1],c=LatC[2],alpha=LatC[3],beta=LatC[4],gamma=LatC[5];
	int hklnr;
	double SinA = sind(alpha), SinB = sind(beta), SinG = sind(gamma), CosA = cosd(alpha), CosB = cosd(beta), CosG = cosd(gamma);
	double GammaPr = acosd((CosA*CosB - CosG)/(SinA*SinB)), BetaPr  = acosd((CosG*CosA - CosB)/(SinG*SinA)), SinBetaPr = sind(BetaPr);
	double Vol = (a*(b*(c*(SinA*(SinBetaPr*(SinG)))))), APr = b*c*SinA/Vol, BPr = c*a*SinB/Vol, CPr = a*b*SinG/Vol;
	double B[3][3]; B[0][0] = APr; B[0][1] = (BPr*cosd(GammaPr)), B[0][2] = (CPr*cosd(BetaPr)), B[1][0] = 0,
		B[1][1] = (BPr*sind(GammaPr)), B[1][2] = (-CPr*SinBetaPr*CosA), B[2][0] = 0, B[2][1] = 0, B[2][2] = (CPr*SinBetaPr*SinA);
	for (hklnr=0;hklnr<nhkls;hklnr++){
		double ginit[3]; ginit[0] = hklsIn[hklnr*4+0]; ginit[1] = hklsIn[hklnr*4+1]; ginit[2] = hklsIn[hklnr*4+2];
		double GCart[3];
		MatrixMult(B,ginit,GCart);
		double Ds = 1/(sqrt((GCart[0]*GCart[0])+(GCart[1]*GCart[1])+(GCart[2]*GCart[2])));
		hkls[hklnr*5+0] = GCart[0];
		hkls[hklnr*5+1] = GCart[1];
		hkls[hklnr*5+2] = GCart[2];
		hkls[hklnr*5+3] = asind((Wavelength)/(2*Ds)); // Theta
		hkls[hklnr*5+4] = hklsIn[hklnr*4+3]; // RingNr
	}
}

static inline void Euler2OrientMat( double Euler[3], double m_out[3][3]){ // Must be in degrees
	double psi, phi, theta, cps, cph, cth, sps, sph, sth;
	psi = Euler[0];
	phi = Euler[1];
	theta = Euler[2];
	cps = cosd(psi) ; cph = cosd(phi); cth = cosd(theta);
	sps = sind(psi); sph = sind(phi); sth = sind(theta);
	m_out[0][0] = cth * cps - sth * cph * sps;
	m_out[0][1] = -cth * cph * sps - sth * cps;
	m_out[0][2] = sph * sps;
	m_out[1][0] = cth * sps + sth * cph * cps;
	m_out[1][1] = cth * cph * cps - sth * sps;
	m_out[1][2] = -sph * cps;
	m_out[2][0] = sth * sph;
	m_out[2][1] = cth * sph;
	m_out[2][2] = cph;
}

static inline void OrientMat2Euler(double m[3][3],double Euler[3]){
    double psi, phi, theta, sph;
	if (fabs(m[2][2] - 1.0) < EPS){
		phi = 0;
	}else{
	    phi = acos(m[2][2]);
	}
    sph = sin(phi);
    if (fabs(sph) < EPS)
    {
        psi = 0.0;
        theta = (fabs(m[2][2] - 1.0) < EPS) ? sin_cos_to_angle(m[1][0], m[0][0]) : sin_cos_to_angle(-m[1][0], m[0][0]);
    } else{
        psi = (fabs(-m[1][2] / sph) <= 1.0) ? sin_cos_to_angle(m[0][2] / sph, -m[1][2] / sph) : sin_cos_to_angle(m[0][2] / sph,1);
        theta = (fabs(m[2][1] / sph) <= 1.0) ? sin_cos_to_angle(m[2][0] / sph, m[2][1] / sph) : sin_cos_to_angle(m[2][0] / sph,1);
    }
    Euler[0] = rad2deg*psi;
    Euler[1] = rad2deg*phi;
    Euler[2] = rad2deg*theta;
}

static inline void RotateAroundZ( double v1[3], double alpha, double v2[3]) {
	double cosa = cos(alpha*deg2rad);
	double sina = sin(alpha*deg2rad);
	double mat[3][3] = {{ cosa, -sina, 0 },
						{ sina,  cosa, 0 },
						{ 0,     0,    1}};
	MatrixMult(mat, v1, v2);
}

static inline void CalcOmega(double x, double y, double z, double theta, double omegas[4], double etas[4], int * nsol) {
	*nsol = 0;
	double ome;
	double len= sqrt(x*x + y*y + z*z);
	double v=sin(theta*deg2rad)*len;
	double almostzero = 1e-4;
	if ( fabs(y) < almostzero ) {
		if (x != 0) {
			double cosome1 = -v/x;
			if (fabs(cosome1 <= 1)) {
				ome = acos(cosome1)*rad2deg;
				omegas[*nsol] = ome;
				*nsol = *nsol + 1;
				omegas[*nsol] = -ome;
				*nsol = *nsol + 1;
			}
		}
	} else {
		double y2 = y*y;
		double a = 1 + ((x*x) / y2);
		double b = (2*v*x) / y2;
		double c = ((v*v) / y2) - 1;
		double discr = b*b - 4*a*c;
		double ome1a;
		double ome1b;
		double ome2a;
		double ome2b;
		double cosome1;
		double cosome2;
		double eqa, eqb, diffa, diffb;
		if (discr >= 0) {
			cosome1 = (-b + sqrt(discr))/(2*a);
			if (fabs(cosome1) <= 1) {
				ome1a = acos(cosome1);
				ome1b = -ome1a;
				eqa = -x*cos(ome1a) + y*sin(ome1a);
				diffa = fabs(eqa - v);
				eqb = -x*cos(ome1b) + y*sin(ome1b);
				diffb = fabs(eqb - v);
				if (diffa < diffb ) {
					omegas[*nsol] = ome1a*rad2deg;
					*nsol = *nsol + 1;
				} else {
					omegas[*nsol] = ome1b*rad2deg;
					*nsol = *nsol + 1;
				}
			}
			cosome2 = (-b - sqrt(discr))/(2*a);
			if (fabs(cosome2) <= 1) {
				ome2a = acos(cosome2);
				ome2b = -ome2a;
				eqa = -x*cos(ome2a) + y*sin(ome2a);
				diffa = fabs(eqa - v);
				eqb = -x*cos(ome2b) + y*sin(ome2b);
				diffb = fabs(eqb - v);
				if (diffa < diffb) {
					omegas[*nsol] = ome2a*rad2deg;
					*nsol = *nsol + 1;
				} else {
					omegas[*nsol] = ome2b*rad2deg;
					*nsol = *nsol + 1;
				}
			}
		}
	}
	double gw[3];
	double gv[3]={x,y,z};
	double eta;
	int indexOme;
	for (indexOme = 0; indexOme < *nsol; indexOme++) {
		RotateAroundZ(gv, omegas[indexOme], gw);
		CalcEtaAngle(gw[1],gw[2], &eta);
		etas[indexOme] = eta;
	}
}


double dx[4] = {-0.5,+0.5,+0.5,-0.5};
double dy[4] = {-0.5,-0.5,+0.5,+0.5};

// Function to calculate the fraction of a voxel in a beam profile. Omega[degrees]
// Assuming a gaussian beam profile.
static inline double IntensityFraction(double voxLen, double beamPosition, double beamFWHM, double voxelPosition[3], double Omega) {
	double xy[4][2], xyPr[4][2], minY=1e6, maxY=-1e6, startY, endY, yStep, intX, volFr=0, sigma, thisPos, delX;
	int inSide=0, nrYs=200, i, j, splCase = 0;
	double omePr,etaPr,eta;
	sigma = beamFWHM/(2*sqrt(2*log(2)));
	// Convert from Omega to Eta (look in the computation notebook, 08/19/19 pg. 34 for calculation)
	if (Omega < 0) omePr = 360 + Omega;
	else omePr = Omega;
	if (abs(omePr) < 1e-5) splCase = 1;
	else if (abs(omePr-90) < 1e-5) splCase = 1;
	else if (abs(omePr-180) < 1e-5) splCase = 1;
	else if (abs(omePr-270) < 1e-5) splCase = 1;
	else if (abs(omePr-360) < 1e-5) splCase = 1;
	else {
		if (omePr < 90) etaPr = 90 - omePr;
		else if (omePr < 180) etaPr = 180 - omePr;
		else if (omePr < 270) etaPr = 270 - omePr;
		else etaPr = 360 - omePr;
		if (etaPr < 45) eta = 90 - etaPr;
		else eta = etaPr;
	}
	// What we need: minY, maxY, startY, endY
	for (i=0;i<4;i++) {
		xy[i][0] = voxelPosition[0] + dx[i]*voxLen;
		xy[i][1] = voxelPosition[1] + dy[i]*voxLen;
		xyPr[i][1] = xy[i][0]*sind(Omega) + xy[i][1]*cosd(Omega);
		if (xyPr[i][1] < minY) {minY = xyPr[i][1];}
		if (xyPr[i][1] > maxY) {maxY = xyPr[i][1];}
	}
	if (maxY >= beamPosition - beamFWHM && minY <= beamPosition + beamFWHM) inSide = 1;
	if (inSide == 1){
		startY = (minY > beamPosition - beamFWHM) ? minY : beamPosition - beamFWHM;
		endY = (maxY < beamPosition + beamFWHM) ? maxY : beamPosition + beamFWHM;
		yStep = (endY-startY)/((double)nrYs);
		for (i=0;i<=nrYs;i++){
			if (splCase == 1) delX = 1;
			else{
				thisPos = i*yStep;
				if (thisPos < voxLen*cosd(eta)) delX = thisPos * (tand(eta)+ (1/tand(eta)));
				else if (maxY-minY - thisPos < voxLen*cosd(eta)) delX = (maxY-minY-thisPos) * (tand(eta)+ (1/tand(eta)));
				else delX = voxLen*(sind(eta)+(cosd(eta)/tand(eta)));
			}
			thisPos = startY + i*yStep;
			intX = yStep*exp(-((thisPos-beamPosition)*(thisPos-beamPosition))/(2*sigma*sigma))/(sigma*sqrt(2*M_PI));
			volFr += intX * delX;
		}
	}
	return volFr;
}

static inline void SpotToGv(double xi, double yi, double zi, double Omega, double theta, double *g1, double *g2, double *g3) {
	double CosOme = cosd(Omega), SinOme = sind(Omega);
	double eta;
	CalcEtaAngle(yi,zi,&eta);
	double TanEta = tand(-eta), SinTheta = sind(theta);
    double CosTheta = cosd(theta), CosW = 1, SinW = 0, k3 = SinTheta*(1+xi)/((yi*TanEta)+zi), k2 = TanEta*k3, k1 = -SinTheta;
    if (eta == 90){
		k3 = 0;
		k2 = -CosTheta;
	} else if (eta == -90){
		k3 = 0;
		k2 = CosTheta;
	}
    double k1f = (k1*CosW) + (k3*SinW), k3f = (k3*CosW) - (k1*SinW), k2f = k2;
    *g1 = (k1f*CosOme) + (k2f*SinOme);
    *g2 = (k2f*CosOme) - (k1f*SinOme);
    *g3 = k3f;
}

// Function to calculate y,z,ome of diffraction spots, given euler angles (degrees), position and lattice parameter.
// Ideal hkls need to be provided, with 4 columns: h,k,l,ringNr
// Output for comparisonType 0: g1,g2,g3,eta,omega,y,z,2theta,nrhkls
static inline long CalcDiffractionSpots(double Lsd, double Wavelength,
			double position[3], double LatC[6], double EulerAngles[3],
			int nhkls, double *hklsIn, double *spotPos, int comparisonType){
	double *hkls; // We need h,k,l,theta,ringNr
	hkls = calloc(nhkls*5,sizeof(*hkls));
	CorrectHKLsLatC(LatC,hklsIn,nhkls,Lsd,Wavelength,hkls);
	double Gc[3],Ghkl[3],cosOme,sinOme,yspot,zspot,yprr;
	double omegas[4], etas[4], lenK, xs, ys, zs, th,g1,g2,g3;
	double yspots, zspots, xGr=position[0], yGr=position[1], zGr=position[2], xRot, yRot, yPrr, zPrr;
	double OM[3][3], theta, RingRadius, omega, eta, etanew, nrhkls;
	int hklnr, nspotsPlane, i;
	Euler2OrientMat(EulerAngles,OM);
	int spotNr = 0;
	for (hklnr=0;hklnr<nhkls;hklnr++){
		Ghkl[0] = hkls[hklnr*5+0];
		Ghkl[1] = hkls[hklnr*5+1];
		Ghkl[2] = hkls[hklnr*5+2];
		MatrixMult(OM,Ghkl, Gc);
		theta = hkls[hklnr*5+3];
		CalcOmega(Gc[0], Gc[1], Gc[2], theta, omegas, etas, &nspotsPlane);
		nrhkls = (double)hklnr*2 + 1;
		for (i=0;i<nspotsPlane;i++){
			omega = omegas[i];
			eta = etas[i];
			if (isnan(omega) || isnan(eta)) continue;
			cosOme = cosd(omega);
			sinOme = sind(omega);
			xRot = xGr*cosOme - yGr*sinOme;
			yRot = xGr*sinOme + yGr*cosOme;
			RingRadius = tand(2*theta)*(Lsd + xRot);
			yPrr = -(sind(eta)*RingRadius);
			zPrr = cosd(eta)*RingRadius;
			yspot = yprr + yRot;
			zspot = zPrr + zGr;
			RingRadius = sqrt(yspot*yspot + zspot*zspot);
			CalcEtaAngle(yspot,zspot,&etanew);
			th = atand(RingRadius/Lsd)/2;
			// Depending on comparisonType:
			// 1. Gvector:
			switch (comparisonType){
			case 1:
				xs = Lsd;
				ys = yspot;
				zs = zspot;
				lenK =CalcNorm3(xs,ys,zs);
				SpotToGv(xs/lenK,ys/lenK,zs/lenK,omega,th,&g1,&g2,&g3);
				spotPos[spotNr*9+0] = g1;
				spotPos[spotNr*9+1] = g2;
				spotPos[spotNr*9+2] = g3;
				spotPos[spotNr*9+3] = etanew;
				spotPos[spotNr*9+4] = omega;
				spotPos[spotNr*9+5] = hkls[hklnr*5+4]; // ringNr
				spotPos[spotNr*9+6] = nrhkls;
				spotPos[spotNr*9+7] = ys;
				spotPos[spotNr*9+8] = zs;
				break;
			case 3:
				spotPos[spotNr*4+0] = yspot;
				spotPos[spotNr*4+1] = zspot;
				spotPos[spotNr*4+2] = omega;
				spotPos[spotNr*4+3] = nrhkls;
				break;
			default:
				spotPos[spotNr*7+6] = 0;
				spotNr = -1;
				nrhkls = -1;
				break;
			}
			nrhkls++;
			spotNr++;
		}
	}
	free(hkls);
	return spotNr;
}

static inline double CalcDifferences(double omegaStep, double px, long totalNrSpots, double *spotInfoMat, double *filteredSpotInfo){
	long i;
	double diff;
	double normParams[3];
	normParams[0] = 0.1*px;
	normParams[1] = 0.1*px;
	for (i=0;i<totalNrSpots;i++){
		if (spotInfoMat[i*4+3] == 0) continue;
		normParams[2] = omegaStep*0.5*(1+1/sind(CalcEta(spotInfoMat[i*4+0],spotInfoMat[i*4+1])));
		diff +=CalcNorm3((spotInfoMat[i*4+0]-filteredSpotInfo[i*4+0])/normParams[0],
						 (spotInfoMat[i*4+1]-filteredSpotInfo[i*4+1])/normParams[1],
						 (spotInfoMat[i*4+2]-filteredSpotInfo[i*4+2])/normParams[2]);
	}
	return diff;
}

// The following function will compute the updated spot position for one voxel and one combination of EulLatC only.
// We will provide it with: voxelPos, EulerLatC, array location to be updated, FLUTstartPos, maxNPos, refArr(used to get position Nr). That's it.
static inline double CalcSpotPosOneVoxOneParam(double omegaStep, double px, double voxelLen, double beamFWHM, int nBeamPositions,
									double *beamPositions, double omeTol, double *EulLatC, int nhkls, double *hkls,
									double Lsd, double Wavelength, double voxelPos[3],
									long *FLUTThis, int *markSpotsMat, long maxNPos, double *refArr, double *spotInfoMat,
									double *filteredSpotInfo, long totalNrSpots, double *spotInfo){
	long i, nSpots, positionNr, bestHKLNr, idxPos, spotNr, spotRowNr;
	double LatCThis[6], EulerThis[3], thisBeamPos,voxelFraction,thisOmega, diff;
	double normParams[3], yMeanUpd, zMeanUpd, omeMeanUpd, newVoxelFr;
	normParams[0] = 0.1*px;
	normParams[1] = 0.1*px;
	for (i=0;i<3;i++) EulerThis[i] = EulLatC[i];
	for (i=0;i<6;i++) LatCThis[i] = EulLatC[i+3];
	nSpots = CalcDiffractionSpots(Lsd,Wavelength,voxelPos,LatCThis,EulerThis,nhkls,hkls,spotInfo,3);
	for (spotNr=0;spotNr<nSpots;spotNr++){
		bestHKLNr = (long)spotInfo[spotNr*4+3];
		thisOmega = spotInfo[spotNr*4+2];
		for (i=0;i<maxNPos;i++){
			if (FLUTThis[bestHKLNr*maxNPos+i] >= 0){
				// We will use the array markSpotsMat to mark the spots we have updated, for those spots, we will calculate the error
					// For the rest we do a for loop at the end.
				idxPos = bestHKLNr*maxNPos*5+i*5;
				positionNr = (long)refArr[idxPos+4];
				thisBeamPos = beamPositions[positionNr];
				voxelFraction = IntensityFraction(voxelLen,thisBeamPos,beamFWHM,voxelPos,thisOmega);
				spotRowNr = FLUTThis[bestHKLNr*maxNPos+i];
				// SetBit the correct position.
				SetBit(markSpotsMat,spotRowNr);
				// Calculate the updated error
				normParams[2] = omegaStep*0.5*(1+1/sind(CalcEta(spotInfo[spotNr*4+0],spotInfo[spotNr*4+1])));
				newVoxelFr = spotInfoMat[spotRowNr*4+3] + voxelFraction - refArr[idxPos+3];
				yMeanUpd =   (spotInfoMat[spotNr*4+0]*spotInfoMat[spotNr*4+3] + spotInfo[spotNr*4+0]*voxelFraction -
									refArr[idxPos+0]*refArr[idxPos+3])/newVoxelFr;
				zMeanUpd =   (spotInfoMat[spotNr*4+1]*spotInfoMat[spotNr*4+3] + spotInfo[spotNr*4+1]*voxelFraction -
									refArr[idxPos+1]*refArr[idxPos+3])/newVoxelFr;
				omeMeanUpd = (spotInfoMat[spotNr*4+2]*spotInfoMat[spotNr*4+3] + spotInfo[spotNr*4+2]*voxelFraction -
									refArr[idxPos+2]*refArr[idxPos+3])/newVoxelFr;
				diff +=CalcNorm3((yMeanUpd  -filteredSpotInfo[spotRowNr*4+0])/normParams[0],
								 (zMeanUpd  -filteredSpotInfo[spotRowNr*4+1])/normParams[1],
								 (omeMeanUpd-filteredSpotInfo[spotRowNr*4+2])/normParams[2]);
			}
		}
	}
	for (i=0;i<totalNrSpots;i++){
		if (TestBit(markSpotsMat,i)){
			ClearBit(markSpotsMat,i); // Unmark the array for use next time.
		} else {
			if (spotInfoMat[i*4+3] == 0) continue;
			normParams[2] = omegaStep*0.5*(1+1/sind(CalcEta(spotInfoMat[i*4+0],spotInfoMat[i*4+1])));
			diff += CalcNorm3((spotInfoMat[i*4+0] - filteredSpotInfo[i*4+0])/normParams[0],
							  (spotInfoMat[i*4+1] - filteredSpotInfo[i*4+1])/normParams[1],
							  (spotInfoMat[i*4+2] - filteredSpotInfo[i*4+2])/normParams[2]);
		}
	}
	free(spotInfo);
	return diff;
}

// The following function will compute the updated spot position for one voxel only.
// We will provide it with: voxelPos, EulerLatC, array location to be updated, FLUTstartPos, maxNPos, spotInfoMat.
static inline void UpdSpotPosOneVox(double omegaStep, double px, double voxelLen, double beamFWHM, int nBeamPositions,
									double *beamPositions, double omeTol, double *EulLatC, int nhkls, double *hkls,
									double Lsd, double Wavelength, double voxelPos[3], double *arrUpd,
									long *FLUTThis, long maxNPos, double *spotInfoMat, double *spotInfo){
	long i, nSpots, positionNr, bestHKLNr, idxPos, spotNr, spotRowNr;
	double LatCThis[6], EulerThis[3], thisBeamPos,voxelFraction,thisOmega, newVoxelFr;
	for (i=0;i<3;i++) EulerThis[i] = EulLatC[i];
	for (i=0;i<6;i++) LatCThis[i] = EulLatC[i+3];
	nSpots = CalcDiffractionSpots(Lsd,Wavelength,voxelPos,LatCThis,EulerThis,nhkls,hkls,spotInfo,3);
	for (spotNr=0;spotNr<nSpots;spotNr++){
		bestHKLNr = (long)spotInfo[spotNr*4+3];
		thisOmega = spotInfo[spotNr*4+2];
		for (i=0;i<maxNPos;i++){
			if (FLUTThis[bestHKLNr*maxNPos+i] >= 0){
				idxPos = bestHKLNr*maxNPos*5+i*5;
				positionNr = (long)arrUpd[idxPos+4];
				thisBeamPos = beamPositions[positionNr];
				voxelFraction = IntensityFraction(voxelLen,thisBeamPos,beamFWHM,voxelPos,thisOmega);
				spotRowNr = FLUTThis[bestHKLNr*maxNPos+i];
				newVoxelFr = spotInfoMat[spotRowNr*4+3] + voxelFraction - arrUpd[idxPos+3];
				#pragma omp critical // This must be serialized, otherwise we would be updating same value with multiple threads at the same time.
				{
					spotInfoMat[spotRowNr*4+0] *= spotInfoMat[spotRowNr*4+3];
					spotInfoMat[spotRowNr*4+0] += spotInfo[spotNr*4+0]*voxelFraction - arrUpd[idxPos+0]*arrUpd[idxPos+3];
					spotInfoMat[spotRowNr*4+0] /= newVoxelFr;
					spotInfoMat[spotRowNr*4+1] *= spotInfoMat[spotRowNr*4+3];
					spotInfoMat[spotRowNr*4+1] += spotInfo[spotNr*4+1]*voxelFraction - arrUpd[idxPos+1]*arrUpd[idxPos+3];
					spotInfoMat[spotRowNr*4+1] /= newVoxelFr;
					spotInfoMat[spotRowNr*4+2] *= spotInfoMat[spotRowNr*4+3];
					spotInfoMat[spotRowNr*4+2] += spotInfo[spotNr*4+2]*voxelFraction - arrUpd[idxPos+2]*arrUpd[idxPos+3];
					spotInfoMat[spotRowNr*4+2] /= newVoxelFr;
					spotInfoMat[spotRowNr*4+3] = newVoxelFr;
				}
				arrUpd[idxPos+0] = spotInfo[spotNr*4+0]; // This is updated for use next time.
				arrUpd[idxPos+1] = spotInfo[spotNr*4+1];
				arrUpd[idxPos+2] = spotInfo[spotNr*4+2];
				arrUpd[idxPos+3] = voxelFraction;
			}
		}
	}
	free(spotInfo);
}

// For each
static inline double UpdateArraysThisLowHigh(double omegaStep, double px, int nVoxels, double *voxelList, double voxelLen,
										double *x, double *x_prev, double beamFWHM, int nBeamPositions, double *beamPositions, double omeTol,
										int nhkls, double *hkls, double Lsd, double Wavelength, double *Fthis,
										long *FLUT, long maxNPos, long totalNrSpots, double *spotInfoMat,
										double *filteredSpotInfo, double *diffLow, double *diffHigh, double h){
	double *spotInfoAll;
	long lenSpotInfoAll;
	lenSpotInfoAll = numProcs;
	lenSpotInfoAll *= nhkls;
	lenSpotInfoAll *= 8;
	spotInfoAll = calloc(lenSpotInfoAll,sizeof(*spotInfoAll));
	long voxelNr;
	# pragma omp parallel num_threads(numProcs)
	{
		#pragma omp for private(voxelNr)
		for (voxelNr=0;voxelNr<nVoxels;voxelNr++){
			double thisParams[9];
			thisParams[0] = x[voxelNr*9+0];
			thisParams[1] = x[voxelNr*9+1];
			thisParams[2] = x[voxelNr*9+2];
			thisParams[3] = x[voxelNr*9+3];
			thisParams[4] = x[voxelNr*9+4];
			thisParams[5] = x[voxelNr*9+5];
			thisParams[6] = x[voxelNr*9+6];
			thisParams[7] = x[voxelNr*9+7];
			thisParams[8] = x[voxelNr*9+8];
			if (thisParams[0] == x_prev[voxelNr*9+0] && thisParams[1] == x_prev[voxelNr*9+1] && thisParams[2] == x_prev[voxelNr*9+2] &&
				thisParams[3] == x_prev[voxelNr*9+3] && thisParams[4] == x_prev[voxelNr*9+4] && thisParams[5] == x_prev[voxelNr*9+5] &&
				thisParams[6] == x_prev[voxelNr*9+6] && thisParams[7] == x_prev[voxelNr*9+7] && thisParams[8] == x_prev[voxelNr*9+8]) // Nothing changed, do nothing.
					continue;
			double voxelPos[3];
			voxelPos[0] = voxelList[voxelNr*2+0];
			voxelPos[1] = voxelList[voxelNr*2+1];
			voxelPos[2] = 0;
			long posFthis, posFLUT, *FLUTVoxel;
			posFthis = voxelNr;
			posFthis *= nhkls+2;
			posFthis *= 2;
			posFthis *= maxNPos;
			posFLUT = posFthis;
			posFthis *= 5;
			double *FthisVoxel;
			FthisVoxel = &Fthis[posFthis];
			FLUTVoxel = &FLUT[posFLUT];
			double *spotInfo;
			long spotInfoPos;
			int tid = omp_get_thread_num();
			spotInfoPos = tid;
			spotInfoPos *= nhkls;
			spotInfoPos *= 8;
			spotInfo = &spotInfoAll[spotInfoPos];
			UpdSpotPosOneVox(omegaStep, px, voxelLen, beamFWHM, nBeamPositions, beamPositions, omeTol, thisParams, nhkls, hkls,
						Lsd, Wavelength, voxelPos, FthisVoxel, FLUTVoxel, maxNPos,spotInfoMat, spotInfo);
		}
	}
	// Calculate the total error! We provide spotInfoMat and filteredSpotInfo
	double diffFThis = CalcDifferences(omegaStep,px,totalNrSpots,spotInfoMat,filteredSpotInfo);
	size_t sizemarkSpotsMat;
	sizemarkSpotsMat = totalNrSpots;
	sizemarkSpotsMat /= 32;
	sizemarkSpotsMat ++;
	sizemarkSpotsMat *= numProcs;
	int *totalMarkSpotsMat;
	totalMarkSpotsMat = calloc(sizemarkSpotsMat,sizeof(*totalMarkSpotsMat));
	# pragma omp parallel num_threads(numProcs)
	{
		#pragma omp for private(voxelNr)
		for (voxelNr=0;voxelNr<nVoxels;voxelNr++){
			double voxelPos[3], xlow[9], xhigh[9];
			xlow[0] = x[voxelNr*9+0];
			xlow[1] = x[voxelNr*9+1];
			xlow[2] = x[voxelNr*9+2];
			xlow[3] = x[voxelNr*9+3];
			xlow[4] = x[voxelNr*9+4];
			xlow[5] = x[voxelNr*9+5];
			xlow[6] = x[voxelNr*9+6];
			xlow[7] = x[voxelNr*9+7];
			xlow[8] = x[voxelNr*9+8];
			voxelPos[0] = voxelList[voxelNr*2+0];
			voxelPos[1] = voxelList[voxelNr*2+1];
			voxelPos[2] = 0;
			xhigh[0] = x[voxelNr*9+0];
			xhigh[1] = x[voxelNr*9+1];
			xhigh[2] = x[voxelNr*9+2];
			xhigh[3] = x[voxelNr*9+3];
			xhigh[4] = x[voxelNr*9+4];
			xhigh[5] = x[voxelNr*9+5];
			xhigh[6] = x[voxelNr*9+6];
			xhigh[7] = x[voxelNr*9+7];
			xhigh[8] = x[voxelNr*9+8];
			long posFthis, posFhl, posFLUT, posTemp, *FLUTVoxel;
			posFthis = voxelNr;
			posFthis *= nhkls+2;
			posFthis *= 2;
			posFthis *= maxNPos;
			posFLUT = posFthis;
			posFthis *= 5;
			double *FthisVoxel;
			FthisVoxel = &Fthis[posFthis];
			FLUTVoxel = &FLUT[posFLUT];
			long i;
			double *spotInfo;
			long spotInfoPos;
			int tid = omp_get_thread_num();
			spotInfoPos = tid;
			spotInfoPos *= nhkls;
			spotInfoPos *= 8;
			spotInfo = &spotInfoAll[spotInfoPos];
			long posMark;
			posMark = totalNrSpots;
			posMark /= 32;
			posMark ++;
			posMark *= tid;
			int *markSpotsMat;
			markSpotsMat = &totalMarkSpotsMat[posMark];
			for (i=0;i<9;i++){
				posTemp = i;
				posTemp *= voxelNr;
				posTemp *= nhkls+2;
				posTemp *= 2;
				posTemp *= 4;
				posFhl = 9;
				posFhl *= voxelNr;
				posFhl *= nhkls+2;
				posFhl *= 2;
				posFhl *= 4;
				posFhl += posTemp;
				xlow[i] -= h;
				xhigh[i] += h;
				diffLow[voxelNr*9+i] = CalcSpotPosOneVoxOneParam(omegaStep, px, voxelLen, beamFWHM, nBeamPositions, beamPositions, omeTol, xlow, nhkls, hkls,
										Lsd, Wavelength, voxelPos, FLUTVoxel, markSpotsMat, maxNPos, FthisVoxel, spotInfoMat,
										filteredSpotInfo, totalNrSpots, spotInfo);
				// Now calculate diffLow[voxelNr*9+i] value
				diffHigh[voxelNr*9+i] = CalcSpotPosOneVoxOneParam(omegaStep, px, voxelLen, beamFWHM, nBeamPositions, beamPositions, omeTol, xhigh, nhkls, hkls,
										Lsd, Wavelength, voxelPos, FLUTVoxel, markSpotsMat, maxNPos, FthisVoxel, spotInfoMat,
										filteredSpotInfo, totalNrSpots, spotInfo);
			}
			x_prev[voxelNr*9+0] = x[voxelNr*9+0];
			x_prev[voxelNr*9+1] = x[voxelNr*9+1];
			x_prev[voxelNr*9+2] = x[voxelNr*9+2];
			x_prev[voxelNr*9+3] = x[voxelNr*9+3];
			x_prev[voxelNr*9+4] = x[voxelNr*9+4];
			x_prev[voxelNr*9+5] = x[voxelNr*9+5];
			x_prev[voxelNr*9+6] = x[voxelNr*9+6];
			x_prev[voxelNr*9+7] = x[voxelNr*9+7];
			x_prev[voxelNr*9+8] = x[voxelNr*9+8];
		}
	}
	free(spotInfoAll);
	free(totalMarkSpotsMat);
	return diffFThis;
}

static inline void PopulateSpotInfoMat (double omegaStep, double px, int nVoxels, double *voxelList, double voxelLen,
									double beamFWHM, int nBeamPositions, double *beamPositions, double omeTol, int nRings,
									double *EulLatC, int nhkls, double *hkls, double Lsd, double Wavelength,
									double *AllSpotsInfo, long *AllIDsInfo, long totalNrSpots, double *spotInfoMat,
									double *Fthis, double *filteredSpotInfo, int maxNPos, long *FLUT){
	long voxelNr, nSpots, i, j, spotNr, positionNr, ringNr;
	double thisPos[3], thisBeamPosition, thisOmega, thisEta, bestAngle, ys, zs, lenK, omeObs;
	double LatCThis[6], EulersThis[3], obsSpotPos[3], gObs[3], gSim[3], IA, bestG1,bestG2,bestG3;
	double *spotInfo, voxelFraction;
	long startRowNr, endRowNr, bestRow, bestHKLNr, idxPos, posNr, *oldArr, *newArr, nFilled, sizeFilled;
	spotInfo = calloc(nhkls*2*9,sizeof(*spotInfo));
	for (voxelNr=0;voxelNr<nVoxels;voxelNr++){
		thisPos[0] = voxelList[voxelNr*2+0];
		thisPos[1] = voxelList[voxelNr*2+1];
		thisPos[2] = 0;
		for (i=0;i<6;i++) LatCThis[i]  = EulLatC[voxelNr*9 + 3 + i];
		for (i=0;i<3;i++) EulersThis[i] = EulLatC[voxelNr*9 + i];
		// Depending on what we want (comparisonType), we can return gVector, 2theta,eta,ome or y,z,ome
		// spotInfo columns acc to comparisonType:
		// 1. g1,		g2,		g3,		eta,	omega,	ringNr,		nrhkls,	y,	z
		// 3. y,		z,		ome,	nrhkls
		nSpots = CalcDiffractionSpots(Lsd,Wavelength,thisPos,LatCThis,EulersThis,nhkls,hkls,spotInfo,1);
		for (spotNr=0;spotNr<nSpots;spotNr++){
			thisOmega = spotInfo[spotNr*9+4];
			thisEta = spotInfo[spotNr*9+3];
			ringNr = (int) spotInfo[spotNr*9+5];
			gSim[0] = spotInfo[spotNr*9+0];
			gSim[1] = spotInfo[spotNr*9+1];
			gSim[2] = spotInfo[spotNr*9+2];
			bestHKLNr = (long)spotInfo[spotNr*9+6];
			posNr = 0;
			for (positionNr=0;positionNr<nBeamPositions;positionNr++){
				thisBeamPosition = beamPositions[positionNr];
				voxelFraction = IntensityFraction(voxelLen,thisBeamPosition,beamFWHM,thisPos,thisOmega);
				if (voxelFraction ==0) continue;
				// Find and set obsSpotPos
				startRowNr = AllIDsInfo[(positionNr*nRings+ringNr)*2+0];
				endRowNr = AllIDsInfo[(positionNr*nRings+ringNr)*2+1];
				bestAngle = 1e10;
				for (i=startRowNr;i<=endRowNr;i++){
					//Everything in AllSpotsInfo needs to have i-1
					omeObs = AllSpotsInfo[14*(i-1)+2];
					if (fabs(thisOmega-omeObs) < omeTol){
						ys = AllSpotsInfo[14*(i-1)+0];
						zs = AllSpotsInfo[14*(i-1)+1];
						lenK = CalcNorm3(Lsd,ys,zs);
						SpotToGv(Lsd/lenK,ys/lenK,zs/lenK,omeObs,AllSpotsInfo[14*(i-1)+7]/2,&gObs[0],&gObs[1],&gObs[2]);
						IA = fabs(acosd((gSim[0]*gObs[0]+gSim[1]*gObs[1]+gSim[2]*gObs[2])/
								(CalcNorm3(gSim[0],gSim[1],gSim[2])*CalcNorm3(gObs[0],gObs[1],gObs[2]))));
						if (IA < bestAngle) {
							// mark this Spot to be used!!!!
							bestAngle = IA;
							bestG1 = gObs[0];
							bestG2 = gObs[1];
							bestG3 = gObs[2];
							bestRow = i;
						}
					}
				}
				if (bestAngle < 1){ // Spot was found
					// We will populate the following arrays now:
						// idxPos = voxelNr*((nhkls+2)*2)*maxNPos*5 + bestHKLNr*maxNPos*5 + posNr*5
						// Fthis:	idxPos + {0,1,2,3} for filling in y,z,ome,frac,positionNr {sim} [simulated positions for each voxel] This is common for each beamPos, except frac and positionNr.
						// FLUT:	voxelNr*(nhkls+2)*2*maxNPos + bestHKLNr*maxNPos + posNr will fill with bestRowNr
						// filteredSpotInfo:	bestRow*3 + {0,1,2} for y,z,ome of each observed spot position. This will correspond to the next 3 arrays
						// spotInfoMat:	bestRow*4 + {0,1,2,3} for y,z,ome,frac mean and total corresponding to each simulated spot.
								// We would need to divide by the total fraction at the end!!!!
					idxPos = voxelNr;
					idxPos *= nhkls+2;
					idxPos *= 2;
					idxPos *= maxNPos;
					idxPos += bestHKLNr*maxNPos;
					idxPos += posNr;
					FLUT[idxPos] = bestRow-1;
					idxPos = voxelNr;
					idxPos *= nhkls+2;
					idxPos *= 2;
					idxPos *= maxNPos;
					idxPos *= 5;
					idxPos += bestHKLNr*maxNPos*5;
					idxPos += posNr*5;
					Fthis[idxPos + 0] = spotInfo[spotNr*9+7];
					Fthis[idxPos + 1] = spotInfo[spotNr*9+8];
					Fthis[idxPos + 2] = spotInfo[spotNr*9+4];
					Fthis[idxPos + 3] = voxelFraction;
					Fthis[idxPos + 4] = positionNr;
					if (filteredSpotInfo[(bestRow-1)*4 + 0] == 0){
						filteredSpotInfo[(bestRow-1)*4 + 0] = AllSpotsInfo[14*(bestRow-1)+0];
						filteredSpotInfo[(bestRow-1)*4 + 1] = AllSpotsInfo[14*(bestRow-1)+1];
						filteredSpotInfo[(bestRow-1)*4 + 2] = AllSpotsInfo[14*(bestRow-1)+2];
					}
					spotInfoMat[(bestRow-1)*4+0] += spotInfo[spotNr*9+7];
					spotInfoMat[(bestRow-1)*4+1] += spotInfo[spotNr*9+8];
					spotInfoMat[(bestRow-1)*4+2] += spotInfo[spotNr*9+4];
					spotInfoMat[(bestRow-1)*4+3] += voxelFraction;
					posNr ++;
				}
			}
		}
	}
	// Divide spotInfoMat with the total fraction!!!!!!!!
	for (i=0;i<totalNrSpots;i++){
		if (spotInfoMat[i*4+3] == 0) continue;
		spotInfoMat[i*4+0] /= spotInfoMat[i*4+3];
		spotInfoMat[i*4+1] /= spotInfoMat[i*4+3];
		spotInfoMat[i*4+2] /= spotInfoMat[i*4+3];
	}
	free(spotInfo);
}

// Parameters to be passed by struct:
struct FITTING_PARAMS {
	double omegaStep,
		px,
		voxelLen,
		beamFWHM,
		omeTol,
		Lsd,
		Wavelength,
		h;
	double *voxelList,
		*x_prev,
		*beamPositions,
		*hkls,
		*Fthis,
		*spotInfoMat,
		*filteredSpotInfo,
		*diffLow,
		*diffHigh;
	int nBeamPositions,
		nhkls;
	long *FLUT;
	long maxNPos,
		totalNrSpots;
};

// EulerAngles in Degrees!!!!
static double problem_function(
	unsigned n,
	const double *x,
	double *grad,
	void* f_data_trial)
{
	int nVoxels = n / 9; // nVoxels*9 is the total number of parameters to be optimized.
	// x is arranged as EulerAngles, then LatC for each voxel. EulerAngle=x[voxelNr*9+{0,1,2}] and LatC=x[voxelNr*9+{3,4,5,6,7,8}].
	struct FITTING_PARAMS *f_data = (struct FITTING_PARAMS *) f_data_trial;
	double omegaStep = f_data->omegaStep, px = f_data->px, voxelLen = f_data->voxelLen, beamFWHM = f_data->beamFWHM, omeTol = f_data->omeTol;
	double Lsd = f_data->Lsd, Wavelength = f_data->Wavelength, h = f_data->h;
	double *voxelList = &(f_data->voxelList[0]), *x_prev = &(f_data->x_prev[0]), *beamPositions = &(f_data->beamPositions[0]), *hkls = &(f_data->hkls[0]);
	double *Fthis = &(f_data->Fthis[0]), *spotInfoMat = &(f_data->spotInfoMat[0]), *filteredSpotInfo = &(f_data->filteredSpotInfo[0]);
	double *diffLow = &(f_data->diffLow[0]), *diffHigh = &(f_data->diffHigh[0]);
	int nBeamPositions = f_data->nBeamPositions, nhkls = f_data->nhkls;
	long *FLUT = &(f_data->FLUT[0]);
	long maxNPos = f_data->maxNPos, totalNrSpots = f_data->totalNrSpots;
	double err;
	err = UpdateArraysThisLowHigh(omegaStep, px, nVoxels, voxelList, voxelLen, x, x_prev, beamFWHM, nBeamPositions, beamPositions, omeTol,
								  nhkls, hkls, Lsd, Wavelength, Fthis, FLUT, maxNPos, totalNrSpots, spotInfoMat, filteredSpotInfo, diffLow, diffHigh, h);
	if (grad){
		int i;
		for (i=0;i<n;i++){
			grad[i] = (diffHigh[i] - diffLow[i])/(2*h);
		}
	}
	return err;
}

int main (int argc, char *argv[]){

	if (argc!=7){
		printf("Usage: ./ScanningFunctions ParameterFile BeamPosition  GrainVoxels    IDsHash    GrainNr numProcs\n"
			   "Eg.	   ./ScanningFunctions  params.txt   positions.csv voxelPos.csv IDsHash.csv    2       64\n");
		return;
	}
	numProcs = argv[6];
	// Read omegaStep, px, voxelLen, beamFWHM, omeTol, Lsd, Wavelength, nScans from PARAM file.
	char *paramFN;
	paramFN = argv[1];
	FILE *fileParam;
	fileParam = fopen(paramFN,"r");
	double omegaStep, px, voxelLen, beamFWHM, omeTol, Lsd, Wavelength;
	int nScans, rings[500], nRings=0;
	char aline[4096], dummy[4096];
	while(fgets(aline,4096,fileParam)!=NULL){
		if (strncmp(aline,"OmegaStep",strlen("OmegaStep"))==0){
			sscanf(aline,"%s %lf",dummy,&omegaStep);
			omegaStep = fabs(omegaStep);
		}
		if (strncmp(aline,"px",strlen("px"))==0){
			sscanf(aline,"%s %lf",dummy,&px);
		}
		if (strncmp(aline,"VoxelLength",strlen("VoxelLength"))==0){
			sscanf(aline,"%s %lf",dummy,&voxelLen);
		}
		if (strncmp(aline,"BeamFWHM",strlen("BeamFWHM"))==0){
			sscanf(aline,"%s %lf",dummy,&beamFWHM);
		}
		if (strncmp(aline,"OmegaTol",strlen("OmegaTol"))==0){
			sscanf(aline,"%s %lf",dummy,&omeTol);
		}
		if (strncmp(aline,"Lsd",strlen("Lsd"))==0){
			sscanf(aline,"%s %lf",dummy,&Lsd);
		}
		if (strncmp(aline,"Wavelength",strlen("Wavelength"))==0){
			sscanf(aline,"%s %lf",dummy,&Wavelength);
		}
		if (strncmp(aline,"nLayers",strlen("nLayers"))==0){
			sscanf(aline,"%s %d",dummy,&nScans);
		}
		if (strncmp(aline,"RingThresh",strlen("RingThresh"))==0){
			sscanf(aline,"%s %d",dummy,&rings[nRings]);
			nRings++;
		}
	}
	fclose(fileParam);

	// Read beamPositions from positions.csv file.
	long i,j,k;
	char *positionsFN;
	positionsFN = argv[2];
	FILE *positionsFile;
	positionsFile = fopen(positionsFN,"r");
	int nBeamPositions = nScans;
	double *beamPositions;
	beamPositions = calloc(nBeamPositions,sizeof(*beamPositions));
	fgets(aline,4096,positionsFile);
	for (i=0;i<nBeamPositions;i++){
		fgets(aline,4096,positionsFile);
		sscanf(aline,"%lf",&beamPositions[i]);
		beamPositions[i] *= 1000;
	}
	fclose(positionsFile);

	// Read hkls, nhkls, nRings (maxRingNr) from HKL file.
	// hkls has h,k,l,ringNr
	char hklfn[4096];
	sprintf(hklfn,"hkls.csv");
	FILE *hklf;
	hklf = fopen(hklfn,"r");
	double ht,kt,lt,ringT;
	double *hklTs;
	hklTs = calloc(500*4,sizeof(*hklTs));
	int nhkls = 0;
	fgets(aline,4096,hklf);
	while (fgets(aline,4096,hklf)!=NULL){
		sscanf(aline,"%lf %lf %lf %s %lf",&ht,&kt,&lt,dummy,&ringT);
		for (i=0;i<nRings;i++){
			if ((int)ringT == rings[i]){
				hklTs[nhkls*4+0] = ht;
				hklTs[nhkls*4+1] = kt;
				hklTs[nhkls*4+2] = lt;
				hklTs[nhkls*4+3] = ringT;
				nhkls++;
			}
		}
	}
	fclose(hklf);
	double *hkls;
	hkls = calloc(nhkls*4,sizeof(*hkls));
	for (i=0;i<nhkls*4;i++) hkls[i] = hklTs[i];
	nRings = (int)hkls[nhkls*4-1]; // Highest ring number
	free(hklTs);

	// Read voxelList from VOXELS file.
	char *voxelsFN;
	voxelsFN = argv[3];
	FILE *voxelsFile;
	voxelsFile = fopen(voxelsFN,"r");
	double *voxelsT;
	voxelsT = calloc(nBeamPositions*nBeamPositions,sizeof(*voxelsT));
	int nVoxels;
	while (fgets(aline,4096,voxelsFile)!=NULL){
		sscanf("%lf,%lf",&voxelsT[nVoxels*2+0],&voxelsT[nVoxels*2+1]);
		nVoxels++;
	}
	double *voxelList;
	voxelList = calloc(nVoxels*2,sizeof(*voxelList));
	for (i=0;i<nVoxels*2;i++) voxelList[i] = voxelsT[i];
	free(voxelsT);

	// Read AllSpotsInfo from ExtraInfo.bin
	char cpCommand[4096];
	sprintf(cpCommand,"cp ExtraInfo.bin /dev/shm");
	system(cpCommand);
	const char *filename = "/dev/shm/ExtraInfo.bin";
	int rc;
	double *AllSpotsInfo;
	struct stat s;
	size_t size;
	int fd = open(filename,O_RDONLY);
	check(fd < 0, "open %s failed: %s", filename, strerror(errno));
	int status = fstat (fd , &s);
	check (status < 0, "stat %s failed: %s", filename, strerror(errno));
	size = s.st_size;
	AllSpotsInfo = mmap(0,size,PROT_READ,MAP_SHARED,fd,0);
	check (AllSpotsInfo == MAP_FAILED,"mmap %s failed: %s", filename, strerror(errno));
	long totalNrSpots = size/(14*sizeof(double));

	// AllIDsInfo is to be filled
	long *AllIDsInfo;
	AllIDsInfo = calloc(nBeamPositions*nRings,sizeof(*AllIDsInfo));
	FILE *idsfile;
	char *idsfn;
	idsfn = argv[4];
	idsfile = fopen(idsfn,"r");
	int positionNr, startNr, endNr, ringNr;
	fgets(aline,4096,idsfile);
	while(fgets(aline,4096,idsfile)!=NULL){
		sscanf(aline,"%d %d %d %d",&positionNr,&ringNr,&startNr,&endNr);
		if (positionNr == 0) continue;
		AllIDsInfo[((positionNr-1)*nRings+ringNr)*2+0] =startNr;
		AllIDsInfo[((positionNr-1)*nRings+ringNr)*2+1] =endNr;
	}
	fclose(idsfile);

	// GrainNr
	int GrainNr = atoi(argv[5]);
	char grainFN[4096];
	sprintf(grainFN,"Grains.csv");
	FILE *grainsFile;
	grainsFile = fopen(grainFN,"r");
	char line[20000];
	for (i=0;i<(9+GrainNr);i++) fgets(line,20000,grainsFile);
	double OM[3][3],LatC[6];
	sscanf(line,"%s %lf %lf %lf %lf %lf %lf %lf %lf %lf %s %s %s %lf %lf %lf %lf %lf %lf",dummy,
		&OM[0][0],&OM[0][1],&OM[0][2],&OM[1][0],&OM[1][1],&OM[1][2],&OM[2][0],&OM[2][1],&OM[2][2],
		&LatC[0],&LatC[1],&LatC[2],&LatC[3],&LatC[4],&LatC[5]);
	double Eul[3];
	OrientMat2Euler(OM,Eul);

	// Setup x
	int n = nVoxels * 9;
	double *x, *xl, *xu;
	double EulTol = 2; // Degrees
	double ABCTol = 2; // %
	double ABGTol = 2; // %
	x = calloc(n,sizeof(*x));
	xl = calloc(n,sizeof(*xl));
	xu = calloc(n,sizeof(*xu));
	for (i=0;i<nVoxels;i++){
		for (j=0;j<3;j++) x[i*9+j] = Eul[j];
		for (j=0;j<3;j++) xl[i*9+j] = Eul[j] - EulTol;
		for (j=0;j<3;j++) xu[i*9+j] = Eul[j] + EulTol;
		for (j=0;j<6;j++) x[i*9+3+j] = LatC[j];
		for (j=0;j<3;j++) xl[i*9+3+j] = LatC[j]*(100-ABCTol)/100;
		for (j=3;j<6;j++) xl[i*9+3+j] = LatC[j]*(100-ABGTol)/100;
		for (j=0;j<3;j++) xu[i*9+3+j] = LatC[j]*(100*ABCTol)/100;
		for (j=3;j<6;j++) xu[i*9+3+j] = LatC[j]*(100+ABGTol)/100;
	}

	// Allocate the arrays:
	// 3 arrays: Fthis, Flow, Fhigh: 9*nVoxels*maxnPos(ceil(2*beamFWHM/voxelLen)+1)*nhkls*2(nDiffrSpots)*4(y,z,ome,fraction)
	// Flow and Fhigh, on the other hand, have 9 times more stuff for one value per parameter.
	// Fthis will have nVoxels*(nhkls+2)*2*maxNPos*5 nrElements because it is constant for each combination of EulLatC for a voxel.
			// Fthis contains also the posNr for each spot. y,z,ome,frac,posNr
	int maxNPos = 2*(2+ceil(2*beamFWHM/voxelLen));
	size_t dataArrSize;
	double *Fthis;
	dataArrSize = nVoxels;
	dataArrSize *= nhkls+2;
	dataArrSize *= 2;
	dataArrSize *= maxNPos;
	dataArrSize *= 5;
	Fthis = calloc(dataArrSize,sizeof(*Fthis));

	// For each spot we will store 5 things: ymean, zmean, omemean, totalfraction,
	// Spot Info LUT: voxelNr,nrhkls and spotInfoNrVoxels (nrVoxels,maxNrVoxels) in another array. maxNrVoxels is only to temproraily store the size of LUT array.
	size_t sizeSpotInfoMat;
	sizeSpotInfoMat = 4;
	sizeSpotInfoMat *= totalNrSpots;
	double *spotInfoMat, *filteredSpotInfo;
	spotInfoMat = calloc(sizeSpotInfoMat,sizeof(*spotInfoMat));
	filteredSpotInfo = calloc(sizeSpotInfoMat,sizeof(*filteredSpotInfo));
	size_t sizeFLUT;
	sizeFLUT = nVoxels;
	sizeFLUT *= nhkls + 2;
	sizeFLUT *= 2;
	sizeFLUT *= maxNPos;
	long *FLUT;
	FLUT = calloc(sizeFLUT,sizeof(*FLUT));
	for (i=0;i<sizeFLUT;i++) FLUT[i] = -1;
	double *diffHigh, *diffLow;
	diffLow = calloc(n,sizeof(*diffLow));
	diffHigh = calloc(n,sizeof(*diffHigh));

	// We will also make an array to hold the previous values, this way we don't need to re-calculate the function if the parameter was not updated.
	// This must be for the full voxel, if the voxel parameters were not updated, we do nothing and continue.
	double *x_prev;
	x_prev = calloc(n,sizeof(*x_prev));
	double h = 1e-5;

	PopulateSpotInfoMat(omegaStep, px, nVoxels, voxelList, voxelLen, beamFWHM, nBeamPositions, beamPositions,
									omeTol, nRings, x, nhkls, hkls, Lsd, Wavelength, AllSpotsInfo, AllIDsInfo,
									totalNrSpots, spotInfoMat, Fthis, filteredSpotInfo, maxNPos, FLUT);

	// For debug, write out Fthis, FLUT, spotInfoMat, filteredSpotInfo
	FILE *ft = fopen("fthis.csv","w"),
		 *fl = fopen("flut.csv","w");
	long nEls;
	nEls = nVoxels;
	nEls *= nhkls + 2;
	nEls *= 2;
	nEls *= maxNPos;
	for (i=0;i<nEls;i++){
		fprintf(ft,"%lf %lf %lf %lf %lf\n",Fthis[i*5+0],Fthis[i*5+1],Fthis[i*5+2],Fthis[i*5+3],Fthis[i*5+4]);
		fprintf(fl,"%l\n",FLUT[i]);
	}
	fclose(ft);
	fclose(fl);
	FILE *fs = fopen("fspotInfo.csv","w"),
		 *ff = fopen("filtered.csv","w");
	for (i=0;i<totalNrSpots;i++){
		fprintf(fs,"%lf %lf %lf %lf\n",spotInfoMat[i*4+0],spotInfoMat[i*4+1],spotInfoMat[i*4+2],spotInfoMat[i*4+3]);
		fprintf(ff,"%lf %lf %lf\n",filteredSpotInfo[i*4+0],filteredSpotInfo[i*4+1],filteredSpotInfo[i*4+2]);
	}
	fclose(fs);
	fclose(ff);
	return;

	struct FITTING_PARAMS f_data;
	f_data.FLUT = &FLUT[0];
	f_data.Fthis = &Fthis;
	f_data.Lsd = Lsd;
	f_data.Wavelength = Wavelength;
	f_data.beamFWHM = beamFWHM;
	f_data.beamPositions = &beamPositions[0];
	f_data.diffLow = &diffLow[0];
	f_data.diffHigh = &diffHigh[0];
	f_data.filteredSpotInfo = &filteredSpotInfo[0];
	f_data.h = h;
	f_data.hkls = &hkls[0];
	f_data.maxNPos = maxNPos;
	f_data.nBeamPositions = nBeamPositions;
	f_data.nhkls = nhkls;
	f_data.omeTol = omeTol;
	f_data.omegaStep = omegaStep;
	f_data.px = px;
	f_data.spotInfoMat = &spotInfoMat[0];
	f_data.totalNrSpots = totalNrSpots;
	f_data.voxelLen = voxelLen;
	f_data.voxelList = &voxelList[0];
	f_data.x_prev = &x_prev[0];
	struct FITTING_PARAMS *f_datat;
	f_datat = &f_data;
	void* trp = (struct FITTING_PARAMS *) f_datat;

	// Now we call the fitting function.
	nlopt_opt opt;
	opt = nlopt_create(NLOPT_LD_MMA, n);
	nlopt_set_lower_bounds(opt, xl);
	nlopt_set_upper_bounds(opt, xu);
	nlopt_set_min_objective(opt, problem_function, trp);
	double minf;
	nlopt_optimize(opt, x, &minf);
	nlopt_destroy(opt);
}