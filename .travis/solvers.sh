#!/bin/bash -x
# Make sure we exit if there is a failure
set -e
: ${SOLVERS?"Solvers must be specified"}

SOLVER_LIST=$(echo "${SOLVERS}" | sed 's/:/ /')

for solver in ${SOLVER_LIST}; do
  echo "Getting solver ${solver}"
  case ${solver} in
  STP)
    echo "STP"
    mkdir stp
    cd stp
    ${KLEE_SRC}/.travis/stp.sh
    cd ../
    ;;
  Z3)
    # FIXME: Move this into its own script
    source ${KLEE_SRC}/.travis/sanitizer_flags.sh
    if [ "X${IS_SANITIZED_BUILD}" != "X0" ]; then
      echo "Error: Requested Sanitized build but Z3 being used is not sanitized"
      exit 1
    fi
    echo "Z3"
    # Should we install libz3-dbg too?
    echo `pwd`
    wget -c https://github.com/Z3Prover/z3/releases/download/Z3-4.8.5/z3-4.8.5-x64-ubuntu-14.04.zip
    unzip z3-4.8.5-x64-ubuntu-14.04.zip
    ;;
  metaSMT)
    echo "metaSMT"
    ${KLEE_SRC}/.travis/metaSMT.sh
    ;;
  *)
    echo "Unknown solver ${solver}"
    exit 1
  esac
done
