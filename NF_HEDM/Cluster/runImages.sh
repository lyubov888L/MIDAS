#!/bin/bash

#
# Copyright (c) 2014, UChicago Argonne, LLC
# See LICENSE file.
#

source ${HOME}/.MIDAS/pathsNF

cmdname=$(basename $0)

if [[ ${#*} != 6 ]];
then
  echo "Usage: ${cmdname} parameterfile nNODEs MachineName"
  echo "Eg. ${cmdname} ParametersFile.txt 6 orthros"
  exit 1
fi

if [[ $1 == /* ]]; then TOP_PARAM_FILE=$1; else TOP_PARAM_FILE=$(pwd)/$1; fi
NCPUS=$2
processImages=1
MACHINE_NAME=$3

nNODES=${NCPUS}
export nNODES

# Go to the right folder
DataDirectory=$( awk '$1 ~ /^DataDirectory/ { print $2 }' ${TOP_PARAM_FILE} )
cd ${DataDirectory}

echo "Reducing images."
NDISTANCES=$( awk '$1 ~ /^nDistances/ { print $2 }' ${TOP_PARAM_FILE} )
NRFILESPERDISTANCE=$( awk '$1 ~ /^NrFilesPerDistance/ { print $2 }' ${TOP_PARAM_FILE} )
NRPIXELS=$( awk '$1 ~ /^NrPixels/ { print $2 }' ${TOP_PARAM_FILE} )
tmpfn=${DataDirectory}/fns.txt
echo "paramfn datadir" > ${tmpfn}
echo "${TOP_PARAM_FILE} ${DataDirectory}" >> ${tmpfn}
${SWIFTDIR}/swift -config ${PFDIR}/sites.conf -sites ${MACHINE_NAME} ${PFDIR}/processLayer.swift \
  -FileData=${tmpfn} -NrDistances=${NDISTANCES} -NrFilesPerDistance=${NRFILESPERDISTANCE} \
  -DoPeakSearch=1 -FFSeedOrientations=${FFSeedOrientations} -DoFullLayer=0