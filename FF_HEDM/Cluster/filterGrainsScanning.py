import sys
from math import fabs
import time
import os

if len(sys.argv) < 2:
	print 'Supply following parameters: PS.txt'
	sys.exit(1)

configFile = sys.argv[1]
pscontent = open(configFile).readlines()
for line in pscontent:
	line = [s for s in line.rstrip().split() if s]
	if len(line) > 0:
		if line[0] == 'PositionsFile':
			positionsFile = os.getcwd() + '/' + line[1]
		elif line[0] == 'OutDirPath':
			outdir = os.getcwd() + '/' + line[1]

t0 = time.time()
grainsFile = open('Grains.csv')
grains = grainsFile.readlines()
grainsFile.close()
spotsFile = open('SpotMatrix.csv')
spotinfo = spotsFile.readline()
uniquegrains = []
grainIDlist = []
spotsList = []
spotsPositions = {}
writearr = []
for line in grains:
	if line[0] == '%' :
		continue
	else:
		e1 = float(line.split()[-3])
		e2 = float(line.split()[-2])
		e3 = float(line.split()[-1])
		if (len(uniquegrains) == 0):
			uniquegrains.append([e1,e2,e3])
			grainIDlist.append([int(line.split()[0])])
			writearr.append([line])
			spotinfo = spotsFile.readline()
			spotsList.append([int(spotinfo.split()[1])])
			spotsPositions[int(spotinfo.split()[1])] = [float(spotinfo.split()[2]),float(spotinfo.split()[3]),float(spotinfo.split()[4])]
			spotinfo = spotsFile.readline()
			while (int(spotinfo.split()[0]) == int(line.split()[0])):
				spotsList[0].append(int(spotinfo.split()[1]))
				spotsPositions[int(spotinfo.split()[1])] = [float(spotinfo.split()[2]),float(spotinfo.split()[3]),float(spotinfo.split()[4])]
				spotinfo = spotsFile.readline()
				if spotinfo == '':
					break
			spotsFile.seek(spotsFile.tell()-len(spotinfo))
			nGrains = 1
		else:
			grainFound = 0
			for grainNr in range(nGrains):
				eG1 = uniquegrains[grainNr][0]
				if (fabs(eG1-e1) < 10): ## 10 degrees tolerance for first euler angle. This is good enough for now.
					grainIDlist[grainNr].append(int(line.split()[0]))
					writearr[grainNr].append(line)
					spotinfo = spotsFile.readline()
					grainFound = 1
					while (int(spotinfo.split()[0]) == int(line.split()[0])):
						spotsList[grainNr].append(int(spotinfo.split()[1]))
						spotsPositions[int(spotinfo.split()[1])] = [float(spotinfo.split()[2]),float(spotinfo.split()[3]),float(spotinfo.split()[4])]
						spotinfo = spotsFile.readline()
						if spotinfo == '':
							break
					spotsFile.seek(spotsFile.tell()-len(spotinfo))
			if grainFound == 0:
				uniquegrains.append([e1,e2,e3])
				grainIDlist.append([int(line.split()[0])])
				writearr.append([line])
				spotinfo = spotsFile.readline()
				spotsList.append([int(spotinfo.split()[1])])
				spotsPositions[int(spotinfo.split()[1])] = [float(spotinfo.split()[2]),float(spotinfo.split()[3]),float(spotinfo.split()[4])]
				spotinfo = spotsFile.readline()
				while (int(spotinfo.split()[0]) == int(line.split()[0])):
					spotsList[nGrains].append(int(spotinfo.split()[1]))
					spotsPositions[int(spotinfo.split()[1])] = [float(spotinfo.split()[2]),float(spotinfo.split()[3]),float(spotinfo.split()[4])]
					spotinfo = spotsFile.readline()
					if spotinfo == '':
						break
				spotsFile.seek(spotsFile.tell()-len(spotinfo))
				nGrains = nGrains + 1
spotsFile.close()

idsFile = open(outdir+'/IDsHash.csv')
idsInfo = idsFile.readlines()

for grainNr in range(nGrains):
	print "Writing Grain " + str(grainNr) + ' of ' + str(nGrains) + ' grains.'
	splist = sorted(set(spotsList[grainNr]))
	f = open('SpotList.csv.' + str(grainNr),'w')
	for sp in splist:
		for line in idsInfo:
			if int(line.split()[2]) <= sp and int(line.split()[3]) >= sp:
				layernr = int(line.split()[0])
				startnr = int(line.split()[4])
				ringnr = int(line.split()[1])
		f2 = open(outdir+'/'+'Layer'+str(layernr)+'/IDRings.csv')
		lines = f2.readlines()
		origID = lines[sp-startnr-1].split()[1]
		print [origID, sp, startnr, layernr, ringnr, lines[sp-startnr-1].split()[0]]
		if (lines[sp-startnr-1].split()[0]!= ringnr):
			print "IDs did not match. Please check."
			sys.exit(1)
		f.write(origID+'\t'+str(sp)+'\t'+str(layernr)+'\t'+str(spotsPositions[sp][0])+'\t'+str(spotsPositions[sp][1])+'\t'+str(spotsPositions[sp][2])+'\n')
	f.close()
	f = open('GrainList.csv.'+str(grainNr),'w')
	for line in writearr[grainNr]:
		f.write(line)
	f.close()

t1 = time.time()
print "Time elapsed: " + str(t1-t0) + " s."