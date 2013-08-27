#PBS -q regular
#PBS -l mppwidth=256
#PBS -l mppnppn=24
#PBS -l walltime=0:25:00
#PBS -N my_job
#PBS -e my_job.$PBS_JOBID.err
#PBS -e my_job.$PBS_JOBID.out
#PBS -j eo
#PBS -V

cd $PBS_O_WORKDIR
aprun -n 256 -N 24 ./elm_5f restart append
