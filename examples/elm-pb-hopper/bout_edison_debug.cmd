#PBS -q debug
#PBS -l mppwidth=512
#PBS -l walltime=0:30:00
#PBS -N my_job
#PBS -j oe
#PBS -e my_job.$PBS_JOBID.err
#PBS -o my_job.$PBS_JOBID.out
#PBS -V

cd $PBS_O_WORKDIR
aprun -n 1024 -j 2 ./elm_pb

