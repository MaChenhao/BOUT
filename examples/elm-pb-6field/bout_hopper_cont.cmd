#PBS -q regular
#PBS -l mppwidth=1024
#PBS -l mppnppn=24
#PBS -l walltime=0:55:00
#PBS -N my_job
#PBS -e my_job.$PBS_JOBID.err
#PBS -e my_job.$PBS_JOBID.out
#PBS -j eo
#PBS -V

cd $PBS_O_WORKDIR
aprun -n 1024 -N 24 ./elm_6f restart append
