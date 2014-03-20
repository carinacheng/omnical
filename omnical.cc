//saved from shanalogcalfast and modified for x5. Original fortran code by Adrian Liu and tranlated to C++ by Shana Tribiano. Improvements made by Jeff Zheng.
#include <stdint.h>
#include <stdio.h>
#include <fstream>
#include <stdexcept>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <stdlib.h>
#include <memory.h>
#include <math.h>
#include <time.h>
#include <algorithm>
#include <functional>
#include <numeric>
#include <calibration_omni.h>
#include <iomanip>

using namespace std;
const string FILENAME = "omnical.cc";
const float PI = atan2(0,-1);
const float SPEEDC = 299.792458;
const bool DBG = false;
const bool TIME = true;
const float MAX_NONE_INF = pow(10, 10);
const float MIN_NONE_ZERO = pow(10, -10);
const double MAX_NONE_INFD = pow(10, 10);
const float MAX_POW_10 = 10;


/******************************************************/
/******************************************************/



/******************************************************/
/******************************************************/
void vecmatmul(vector<vector<double> > * Afitting, vector<float> * v, vector<float> * ampfit){
	int i, j;
	double sum;
	int n = Afitting->size();//todo size check
	int m = v->size();
	for(i=0; i < n; i++){
		sum = 0.0;
		for(j = 0; j < m; j++){
			sum = sum + (Afitting->at(i))[j] * (v->at(j));
		}
		(ampfit->at(i)) = sum;
	}
	return;
}

void vecmatmul(vector<vector<float> > * Afitting, vector<float> * v, vector<float> * ampfit){
	int i, j;
	double sum;
	int n = Afitting->size();//todo size check
	int m = v->size();
	for(i=0; i < n; i++){
		sum = 0.0;
		for(j = 0; j < m; j++){
			sum = sum + (Afitting->at(i))[j] * (v->at(j));
		}
		(ampfit->at(i)) = sum;
	}
	return;
}

void vecmatmul(vector<vector<int> > * A, vector<float> * v, vector<float> * yfit){
	int i, j;
	double sum;
	int n = A->size();//todo size check
	int m = v->size();
	for(i=0; i < n; i++){
		sum = 0.0;
		for(j = 0; j < m; j++){
			sum = sum + (A->at(i))[j] * (v->at(j));
		}
		(yfit->at(i)) = sum;
	}
	return;
}


/******************************************************/
/******************************************************/

void updateadditive(vector<vector<float> >* additiveresult, vector<vector<vector<float> > >* additiveXX, redundantinfo* info, calmemmodule* module){////fast negligible time
	fill(module->adata1.begin(), module->adata1.end(), vector<float>(module->adata1[0].size(), 0));
	for (int t = 0; t < additiveXX->size(); t++){
		for (int i = 0; i < additiveXX->at(t).size(); i++){
			module->adata1[i][0] += additiveXX->at(t)[i][0];
			module->adata1[i][1] += additiveXX->at(t)[i][1];
		}
	}
	for (int i = 0; i < additiveresult->size(); i++){
		additiveresult->at(i)[0] += module->adata1[i][0] / additiveXX->size();
		additiveresult->at(i)[1] += module->adata1[i][1] / additiveXX->size();
	}
	return;
}

void logcaladd(vector<vector<float> >* data, vector<vector<float> >* additivein, redundantinfo* info, vector<float>* calpar, vector<vector<float> >* additiveout, int command, calmemmodule* module){
	int nant = info->nAntenna;
	int nubl = info->nUBL;
	int nbl = nant * (nant + 1) / 2;
	int ncross = nbl - nant;
	
	////read in amp and args
	for (int b = 0; b < ncross; b++){
		module->amp1[b] = log10(amp(data->at(info->crossindex[b])[0] - additivein->at(info->crossindex[b])[0], data->at(info->crossindex[b])[1] - additivein->at(info->crossindex[b])[1]));
		module->pha1[b] = phase(data->at(info->crossindex[b])[0] - additivein->at(info->crossindex[b])[0], data->at(info->crossindex[b])[1] - additivein->at(info->crossindex[b])[1]) * info->reversed[b];
	}
	
	////rewrap args
	for(int i = 0; i < nubl; i ++){
		for(int j = 0; j < (module->ublgrp1)[i].size(); j ++){
			(module->ublgrp1)[i][j] = module->pha1[info->ublindex[i][j][2] - info->ublindex[i][j][0] - 1];
		}
	}
	for (int i = 0; i < nubl; i++){
		(module->ubl1)[i][1] = medianAngle(&((module->ublgrp1)[i]));
	}
	for (int b = 0; b < ncross; b++) {
		module->pha1[b] = phaseWrap(module->pha1[b], (module->ubl1)[info->bltoubl[b]][1] - PI);
	}
	

	fill(module->x3.begin(), module->x3.end(), 0);////At.y
	for (int i = 0; i < info->Atsparse.size(); i++){
		for (int j = 0; j < info->Atsparse[i].size(); j++){
			module->x3[i] += module->amp1[info->Atsparse[i][j]];
		}
	}
	fill(module->x4.begin(), module->x4.end(), 0);////Bt.y
	for (int i = 0; i < info->Btsparse.size(); i++){
		for (int j = 0; j < info->Btsparse[i].size(); j++){
			module->x4[i] += module->pha1[info->Btsparse[i][j][0]] * info->Btsparse[i][j][1];
		}
	}
	vecmatmul(&(info->AtAi), &(module->x3), &(module->x1));
	vecmatmul(&(info->BtBi), &(module->x4), &(module->x2));
	
	for(int b = 0; b < ncross; b++) {
		float amp = pow10(module->x1[nant + info->bltoubl[b]] + module->x1[info->bl2d[info->crossindex[b]][0]] + module->x1[info->bl2d[info->crossindex[b]][1]]);
		float phase =  module->x2[nant + info->bltoubl[b]] * info->reversed[b] - module->x2[info->bl2d[info->crossindex[b]][0]] + module->x2[info->bl2d[info->crossindex[b]][1]];
		additiveout->at(info->crossindex[b])[0] = data->at(info->crossindex[b])[0] - additivein->at(info->crossindex[b])[0] - amp * cos(phase);
		additiveout->at(info->crossindex[b])[1] = data->at(info->crossindex[b])[1] - additivein->at(info->crossindex[b])[1] - amp * sin(phase);
	}
	if(command == 0){////compute additive term only
		calpar->at(0) = pow(norm(additiveout), 2);
		//cout << norm(additiveout) << endl;
		return;
	} else if(command == 1){////compute full set of calpars
		for(int a = 0; a < nant; a++){
			calpar->at(3 + a) = module->x1[a];
			calpar->at(3 + nant + a) = module->x2[a];
		}
		for(int u = 0; u < nubl; u++){
			calpar->at(3 + 2 * nant + 2 * u) = pow10(module->x1[nant + u]) * cos(module->x2[nant + u]);
			calpar->at(3 + 2 * nant + 2 * u + 1) = pow10(module->x1[nant + u]) * sin(module->x2[nant + u]);
		}
		calpar->at(1) = pow(norm(additiveout), 2);
	}
	return;
}

vector<float> minimizecomplex(vector<vector<float> >* a, vector<vector<float> >* b){
	vector<float> sum1(2, 0);
	for(int i =0; i < a->size(); i++){
		sum1[0] += a->at(i)[0] * b->at(i)[0] + a->at(i)[1] * b->at(i)[1];
		sum1[1] += a->at(i)[1] * b->at(i)[0] - a->at(i)[0] * b->at(i)[1];
	}
	float sum2 = pow(norm(b), 2);
	sum1[0] = sum1[0] / sum2;
	sum1[1] = sum1[1] / sum2;
	return sum1;
}

void lincal(vector<vector<float> >* data, vector<vector<float> >* additivein, redundantinfo* info, vector<float>* calpar, calmemmodule* module, float convergethresh, int maxiter, float stepsize){
	////initialize data and g0 ubl0
	for (int b = 0; b < (module->cdata1).size(); b++){
		module->cdata1[b][0] = data->at(info->crossindex[b])[0] - additivein->at(info->crossindex[b])[0];
		module->cdata1[b][1] = data->at(info->crossindex[b])[1] - additivein->at(info->crossindex[b])[1];
	}
	float amptmp;
	int cbl;
	float stepsize2 = 1 - stepsize;
	for (int a = 0; a < info->nAntenna; a++){
		amptmp = pow10(calpar->at(3 + a));
		module->g0[a][0] = amptmp * cos(calpar->at(3 + info->nAntenna + a));
		module->g0[a][1] = amptmp * sin(calpar->at(3 + info->nAntenna + a));
	}
	for (int u = 0; u < info->nUBL; u++){
		module->ubl0[u][0] = calpar->at(3 + 2 * info->nAntenna + 2 * u);
		module->ubl0[u][1] = calpar->at(3 + 2 * info->nAntenna + 2 * u + 1);
	}
	
	////start iterations
	int iter = 0;
	float componentchange = 100;
	while(iter < maxiter and componentchange > convergethresh){
		iter++;
		
		////calpar g
		for (int a3 = 0; a3 < module->g3.size(); a3++){////g3 will be containing the final dg, g1, g2 will contain a and b as in the cost function LAMBDA = ||a + b*g||^2
			for (int a = 0; a < a3; a++){
				cbl = info->bl1dmatrix[a3][a] - a - 1;
				module->g1[a] = module->cdata1[cbl];
				module->g2[a][0] = (module->g0[a][0] * module->ubl0[info->bltoubl[cbl]][0] + module->g0[a][1] * module->ubl0[info->bltoubl[cbl]][1] * info->reversed[cbl]);
				module->g2[a][1] = (module->g0[a][0] * module->ubl0[info->bltoubl[cbl]][1] * info->reversed[cbl] - module->g0[a][1] * module->ubl0[info->bltoubl[cbl]][0]);
			}
			(module->g1)[a3] = vector<float>(2,0);
			(module->g2)[a3] = (module->g1)[a3];
			for (int a = a3 + 1; a < module->g3.size(); a++){
				cbl = info->bl1dmatrix[a3][a] - a3 - 1;
				module->g1[a][0] = module->cdata1[cbl][0];
				module->g1[a][1] = -module->cdata1[cbl][1];////vij needs to be conjugated
				module->g2[a][0] = (module->g0[a][0] * module->ubl0[info->bltoubl[cbl]][0] + module->g0[a][1] * module->ubl0[info->bltoubl[cbl]][1] * (-info->reversed[cbl]));////Mi-j needs to be conjugated
				module->g2[a][1] = (module->g0[a][0] * module->ubl0[info->bltoubl[cbl]][1] * (-info->reversed[cbl]) - module->g0[a][1] * module->ubl0[info->bltoubl[cbl]][0]);
			}
			module->g3[a3] = minimizecomplex(&(module->g1), &(module->g2));
			//if(a3 == module->g3.size() - 2) printvv(&(module->g1));
			//if(a3 == module->g3.size() - 2) printvv(&(module->g2));
		}
		//printvv(&(module->g0),0,10);
		//printvv(&(module->g3),0,10);
		
		////ubl M
		for (int u = 0; u < info->nUBL; u++){
			for (int i = 0; i < module->ubl2dgrp1[u].size(); i++){
				cbl = info->ublindex[u][i][2] - info->ublindex[u][i][0] - 1;
				module->ubl2dgrp1[u][i][0] = module->cdata1[cbl][0];
				module->ubl2dgrp1[u][i][1] = module->cdata1[cbl][1] * info->reversed[cbl];
				module->ubl2dgrp2[u][i][0] = module->g0[info->ublindex[u][i][0]][0] * module->g0[info->ublindex[u][i][1]][0] + module->g0[info->ublindex[u][i][0]][1] * module->g0[info->ublindex[u][i][1]][1];
				module->ubl2dgrp2[u][i][1] = (module->g0[info->ublindex[u][i][0]][0] * module->g0[info->ublindex[u][i][1]][1] - module->g0[info->ublindex[u][i][0]][1] * module->g0[info->ublindex[u][i][1]][0]) * info->reversed[cbl];
			}
			
			module->ubl3[u] = minimizecomplex(&(module->ubl2dgrp1[u]), &(module->ubl2dgrp2[u]));
		}
		//printvv(&(module->ubl3));
		//printvv(&(module->ubl2dgrp1[35]));
		//printvv(&(module->ubl2dgrp2[35]));
		//printv(&(module->ubl3[35]));
		
		////compute fractional change and then update g and ubl
		componentchange = 0;
		float fraction;
		//printvv(&(module->ubl0),0,5);
		//printvv(&(module->ubl3),0,5);
		for (int a = 0; a < module->g3.size(); a++){
			fraction = amp(module->g3[a][0] - module->g0[a][0], module->g3[a][1] - module->g0[a][1]) / amp(module->g0[a][0], module->g0[a][1]);
			if (fraction > componentchange){
				componentchange = fraction;
			}
			
			module->g0[a][0] = stepsize2 * module->g0[a][0] + stepsize * module->g3[a][0];
			module->g0[a][1] = stepsize2 * module->g0[a][1] + stepsize * module->g3[a][1];
		}
		for (int u = 0; u < module->ubl3.size(); u++){
			fraction = amp(module->ubl3[u][0] - module->ubl0[u][0], module->ubl3[u][1] - module->ubl0[u][1]) / amp(module->ubl0[u][0], module->ubl0[u][1]);
			if (fraction > componentchange){
				componentchange = fraction;
			}
			module->ubl0[u][0] = stepsize2 * module->ubl0[u][0] + stepsize * module->ubl3[u][0];
			module->ubl0[u][1] = stepsize2 * module->ubl0[u][1] + stepsize * module->ubl3[u][1];
		}
	}


	////update calpar
	for (int a = 0; a < module->g0.size(); a++){
		calpar->at(3 + a) = log10(amp(&(module->g0[a])));
		calpar->at(3 + info->nAntenna + a) = phase(&(module->g0[a]));
	}
	int tmp = 3 + 2 * info->nAntenna;
	for (int u = 0; u < module->ubl0.size(); u++){
		calpar->at(tmp + 2 * u) = module->ubl0[u][0];
		calpar->at(tmp + 2 * u + 1) = module->ubl0[u][1];
	}
	////compute Ax and chisq
	
	float gre, gim;
	int a1, a2;
	float chisq = 0;
	for (int b = 0; b < (module->cdata2).size(); b++){
		a1 = info->bl2d[info->crossindex[b]][0];
		a2 = info->bl2d[info->crossindex[b]][1];
		gre = module->g0[a1][0] * module->g0[a2][0] + module->g0[a1][1] * module->g0[a2][1];
		gim = module->g0[a1][0] * module->g0[a2][1] - module->g0[a1][1] * module->g0[a2][0];
		module->cdata2[b][0] = gre * module->ubl0[info->bltoubl[b]][0] - gim * module->ubl0[info->bltoubl[b]][1] * info->reversed[b];
		module->cdata2[b][1] = gre * module->ubl0[info->bltoubl[b]][1] * info->reversed[b] + gim * module->ubl0[info->bltoubl[b]][0];
		chisq += (pow(module->cdata2[b][0] - module->cdata1[b][0], 2) + pow(module->cdata2[b][1] - module->cdata1[b][1], 2));
		//cout << gre << " " << gim << " " << module->ubl0[info->bltoubl[b]][0] << " " << module->ubl0[info->bltoubl[b]][1] * info->reversed[b] << " " <<  a1 << " " <<  a2 << " " <<  b << " " << info->reversed[b] << endl;
	}
	//string opname = "/mnt/hgfs/data/x5/tmp.dat";
	//outputAscii(&(module->g0), opname, 0);
	//outputAscii(&(module->ubl0), opname);
	//outputAscii(&(module->cdata2), opname);
	//printvv(&(module->ubl0),0,10);
	//printvv(&(module->cdata2),0,10);
	calpar->at(2) = chisq;
	//cout << chisq << endl;
	return;
}

void loadGoodVisibilities(vector<vector<vector<vector<float> > > > * rawdata, vector<vector<vector<vector<float> > > >* receiver, redundantinfo* info, int xy){////0 for xx 3 for yy
	for (int t = 0; t < receiver->size(); t++){
		for (int f = 0; f < receiver->at(0).size(); f++){
			for (int bl = 0; bl < receiver->at(0)[0].size(); bl++){
				receiver->at(t)[f][bl][0] = rawdata->at(xy)[t][f][2 * info->subsetbl[bl]];
				receiver->at(t)[f][bl][1] = rawdata->at(xy)[t][f][2 * info->subsetbl[bl] + 1];
			}
		}		
	}
	return;
}

int main(int argc, char *argv[]){
	string METHODNAME = "main";
	if (argc ! = 4){
		cout << "##" << FILENAME << "##" << METHODNAME << "FALTAL ERROR: Incorrect input format! Expecting data path, info path, time count, frequency count." << endl;
		return 0;
	}
	string visin = "/rawcal_visibilities";
	string visoutXX = "/redcal_visibilitiesXX";
	string calparoutXX = "/redcalparXX";
	string visoutYY = "/redcal_visibilitiesYY";
	string calparoutYY = "/redcalparYY";
	string addoutXX = "/additiveXX.dat";
	string addoutYY = "/additiveYY.dat";
	vector<string> odfSet = parseLines(exec("cat logcal_todoq4A.txt"));
	
	string infopathx = "redundantinfo_q4x.txt";
	string infopathy = "redundantinfo_q4y.txt";

	if (odfSet.size() < 1 ) exit(1);
	cout << "##" << FILENAME << "##" << METHODNAME << ": Reading redundant baseline information and pre-computed matrices:" << endl;//generated from 16. additive noise investigation _from_17.nb
	redundantinfo infoXX, infoYY;
	readredundantinfo(infopathx, &infoXX);
	readredundantinfo(infopathy, &infoYY);
	cout << "XX: Good antenna count: " << infoXX.nAntenna << ". UBL count: " << infoXX.nUBL << "." << endl;
	cout << "YY: Good antenna count: " << infoYY.nAntenna << ". UBL count: " << infoYY.nUBL << "." << endl;
	cout.flush();
	
	cout << "##" << FILENAME << "##" << METHODNAME << ": Reading header.txt from first odf" << endl;
	string outputPath, outputRawPath, outputcalparPath, odfaPath;

	odfheader header;
	string odfPath = odfSet[0];
	odfaPath = odfPath;
	size_t found = odfaPath.find(".odf");
	odfaPath.replace(found, 4, ".odfa");
	cout << odfPath << endl << odfaPath << endl;
	if (!readODFHeader(odfPath, &header)){
		cout << "##" << FILENAME << "##" << METHODNAME << ": FATAL ERROR: Unable to read header in " << odfPath << ". Exiting!: " << endl;
		return 0;
	}
	int nBaseline = header.nChannel * (header.nChannel + 1) / 2;
	int nCross = header.nChannel * (header.nChannel -1) / 2;
	
	////////////////read antloc and generate antlocx and antlocy with bad ant removed 
	vector<vector<float> > antloc (header.nChannel, vector<float> (3, 0));
	vector<vector<float> > cablelen (2, vector<float> (header.nChannel, 0));
	readAntloc((odfaPath + "/antlocx5.dat").c_str(), &antloc, &cablelen, header.nChannel);

	vector<vector<float> > antlocXX, antlocYY;
	for (int i = 0; i < infoXX.subsetant.size(); i++){
		antlocXX.push_back(antloc[infoXX.subsetant[i]]);
	}
	for (int i = 0; i < infoYY.subsetant.size(); i++){
		antlocYY.push_back(antloc[infoYY.subsetant[i]]);
	}	



	////allocate big memories for calibration operations
	cout << "##" << FILENAME << "##" << METHODNAME << ": Allocating big memories for calibration operations...";
	cout.flush();
	vector<vector<vector<vector<float> > > > rawdata(4, vector<vector<vector<float> > >(header.nIntegration, vector<vector<float> >(header.nFrequency, vector<float>(2 * nBaseline, 0))));
	vector<vector<vector<vector<float> > > > dataXX(header.nIntegration, vector<vector<vector<float> > >(header.nFrequency, vector<vector<float> >(infoXX.subsetbl.size(), vector<float>(2, 0))));
	vector<vector<vector<float> > > calparXX(header.nIntegration, vector<vector<float> >(header.nFrequency, vector<float>(3 + 2*(infoXX.nUBL + infoXX.nAntenna), 0)));
	vector<vector<vector<vector<float> > > > dataYY(header.nIntegration, vector<vector<vector<float> > >(header.nFrequency, vector<vector<float> >(infoYY.subsetbl.size(), vector<float>(2, 0))));
	vector<vector<vector<float> > > calparYY(header.nIntegration, vector<vector<float> >(header.nFrequency, vector<float>(3 + 2*(infoYY.nUBL + infoYY.nAntenna), 0)));
	int nchunk = round(header.nIntegration * header.integrationTime / 300);
	int chunksize = header.nIntegration / nchunk;////No. of time slices to average for additive

	vector<vector<vector<float> > > additiveXX(chunksize, vector<vector<float> >(dataXX[0][0].size(), vector<float>(dataXX[0][0][0].size(), 0)));////no frequency dimension, reused memory for holding additive term before averaging
	vector<vector<vector<float> > > additiveresultXX(nchunk, vector<vector<float> >(dataXX[0][0].size(), vector<float>(dataXX[0][0][0].size(), 0)));
	vector<vector<vector<float> > > additiveYY(chunksize, vector<vector<float> >(dataYY[0][0].size(), vector<float>(dataYY[0][0][0].size(), 0)));////no frequency dimension, reused memory for holding additive term before averaging
	vector<vector<vector<float> > > additiveresultYY(nchunk, vector<vector<float> >(dataYY[0][0].size(), vector<float>(dataYY[0][0][0].size(), 0)));
	
	calmemmodule moduleXX, moduleYY;////memory module to be reused in order to avoid redeclaring all sorts of long vectors
	initcalmodule(&moduleXX, infoXX.nAntenna, &(infoXX.ublcount));
	initcalmodule(&moduleYY, infoYY.nAntenna, &(infoYY.ublcount));
	cout << "Done." << endl;
	cout.flush();




	////////////Start calibration///////////
	for (int o = 0; o < odfSet.size(); o++){
		clock_t tStart = clock();
		odfPath = odfSet[o];
		odfaPath = odfPath;
		size_t found = odfaPath.find(".odf");
		odfaPath.replace(found, 4, ".odfa");
		if (!readODFHeader(odfPath, &header)){
			cout << "##" << FILENAME << "##" << METHODNAME << ": FATAL ERROR: Unable to read header in " << odfPath << ". Exiting!: " << endl;
			return 0;
		}
		odfheader_print(&header);
		if (rawdata[0].size() != header.nIntegration){
			for (int i = 0; i < 10; i++) cout << "FATAL ERROR_";
			cout << endl << "##" << FILENAME << "##" << METHODNAME << ": FATAL ERROR: Number of integrations changed from " << rawdata[0].size() << " to " << header.nIntegration << ". Exiting!: " << endl;
		}
		readBinaryVisibilityLarge((odfPath + visin).c_str(), &rawdata, 4, header.nIntegration, header.nFrequency, nBaseline);
		loadGoodVisibilities(&rawdata, &dataXX, &infoXX, 0);
		loadGoodVisibilities(&rawdata, &dataYY, &infoYY, 3);

		for (int f = 0; f < dataXX[0].size(); f++){
			for (int c = 0; c < nchunk; c++){
				////initialize
				fill(additiveresultXX[c].begin(), additiveresultXX[c].end(), vector<float>(2, 0));
				////compute
				for (int t = c * chunksize; t < (c + 1) * chunksize; t++){
					logcaladd(&(dataXX[t][f]), &(additiveresultXX[c]), &infoXX, &(calparXX[t][f]), &(additiveXX[t - c * chunksize]), 0, &moduleXX);
				}
				////update the average
				updateadditive(&(additiveresultXX[c]), &additiveXX, &infoXX, &moduleXX);
				////subtract average and logcal again
				for (int t = c * chunksize; t < (c + 1) * chunksize; t++){
					logcaladd(&(dataXX[t][f]), &(additiveresultXX[c]), &infoXX, &(calparXX[t][f]), &(additiveXX[t - c * chunksize]), 1, &moduleXX);
				}
				//////update the average again
				//updateadditive(&(additiveresult[c]), &additiveXX, &info, &module);
				
				////lincal
				for (int t = c * chunksize; t < (c + 1) * chunksize; t++){
					lincal(&(dataXX[t][f]), &(additiveresultXX[c]), &infoXX, &(calparXX[t][f]), &moduleXX, 0.05, 3, 0.5);
				}
			}
			outputAscii(&additiveresultXX, odfaPath + addoutXX, f, false);//f=0 then overwrite
		
		}
		for (int f = 0; f < dataYY[0].size(); f++){
			for (int c = 0; c < nchunk; c++){
				////initialize
				fill(additiveresultYY[c].begin(), additiveresultYY[c].end(), vector<float>(2, 0));
				////compute
				for (int t = c * chunksize; t < (c + 1) * chunksize; t++){
					logcaladd(&(dataYY[t][f]), &(additiveresultYY[c]), &infoYY, &(calparYY[t][f]), &(additiveYY[t - c * chunksize]), 0, &moduleYY);
				}
				////update the average
				updateadditive(&(additiveresultYY[c]), &additiveYY, &infoYY, &moduleYY);
				////subtract average and logcal again
				for (int t = c * chunksize; t < (c + 1) * chunksize; t++){
					logcaladd(&(dataYY[t][f]), &(additiveresultYY[c]), &infoYY, &(calparYY[t][f]), &(additiveYY[t - c * chunksize]), 1, &moduleYY);
				}
				//////update the average again
				//updateadditive(&(additiveresult[c]), &additiveYY, &info, &module);
				
				////lincal
				for (int t = c * chunksize; t < (c + 1) * chunksize; t++){
					lincal(&(dataYY[t][f]), &(additiveresultYY[c]), &infoYY, &(calparYY[t][f]), &moduleYY, 0.05, 3, 0.5);
				}
			}
			outputAscii(&additiveresultYY, odfaPath + addoutYY, f, false);
		}

		
		outputCalparSP(&calparXX, odfaPath + calparoutXX, false, antlocXX.size());
		//outputHeader(1, odfPath + calparoutXX, 2, &antlocXX);
		outputCalparSP(&calparYY, odfaPath + calparoutYY, false, antlocYY.size());
		//outputHeader(1, odfPath + calparoutYY, 2, &antlocYY);

		printf("Time taken: %.2fs\n", (double)(clock() - tStart)/CLOCKS_PER_SEC);
	}
	return 0;
}
