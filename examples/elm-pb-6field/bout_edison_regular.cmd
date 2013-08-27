#PBS -q regular
#PBS -l mppwidth=512
#PBS -l walltime=1:30:00
#PBS -N my_job
#PBS -j oe
#PBS -e my_job.$PBS_JOBID.err
#PBS -o my_job.$PBS_JOBID.out
#PBS -V

cd $PBS_O_WORKDIR
aprun -n 512 ./elm_6f

