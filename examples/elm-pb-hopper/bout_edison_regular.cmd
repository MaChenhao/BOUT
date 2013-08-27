#PBS -q regular
#PBS -l mppwidth=128
#PBS -l walltime=1:00:00
#PBS -N my_job
#PBS -j oe
#PBS -e my_job.$PBS_JOBID.err
#PBS -o my_job.$PBS_JOBID.out
#PBS -V

cd $PBS_O_WORKDIR
aprun -n 64 -j 2 ./elm_pb

