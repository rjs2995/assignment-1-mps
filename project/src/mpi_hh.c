/*
  Multiple Processor Systems. Spring 2018
  Professor Muhammad Shaaban
  Author: Dmitri Yudanov (update: Dan Brandt)

  This is a Hodgkin Huxley (HH) simplified compartamental neuron model
*/
#include "plot.h"
#include "lib_hh.h"
#include "cmd_args.h"
#include "constants.h"

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "mpi.h"
// Define macros based on compilation options. This is a best practice that
// ensures that all code is seen by the compiler so there will be no surprises
// when a flag is/isn't defined. Any modern compiler will compile out any
// unreachable code.
#ifdef PLOT_SCREEN
#define ISDEF_PLOT_SCREEN 1
#else
#define ISDEF_PLOT_SCREEN 0
#endif

#ifdef PLOT_PNG
#define ISDEF_PLOT_PNG 1
#else
#define ISDEF_PLOT_PNG 0
#endif

/**
 * Name: main
 *
 * Description:
 * See usage statement (run program with '-h' flag).
 *
 * Parameters:
 * @param argc    number of command line arguments
 * @param argv    command line arguments
 */
int main(int argc, char **argv)
{
  CmdArgs cmd_args;                           // Command line arguments.
  int num_comps, num_dendrs;                  // Simulation parameters.
  int i, j, t_ms, step, dendrite, rank, size; // Various indexing variables.
  struct timeval start, stop, diff;           // Values used to measure time.

  double exec_time; // How long we take.

  // Accumulators used during dendrite simulation.
  // NOTE: We depend on the compiler to handle the use of double[] variables as
  //       double*.
  double current, **dendr_volt;
  double res[COMPTIME], y[NUMVAR], y0[NUMVAR], dydt[NUMVAR], soma_params[3];

  // Strings used to store filenames for the graph and data files.
  char time_str[14];
  char graph_fname[FNAME_LEN];
  char data_fname[FNAME_LEN];
  char my_name[100]; // Local processor name
  int my_name_len;   // Size of local processor name
                     // Message packaging
  FILE *data_file;   // The output file where we store the soma potential values.
  FILE *graph_file;  // File where graph will be saved.

  PlotInfo pinfo; // Info passed to the plotting functions.
  MPI_Status status;
   if (!parseArgs( &cmd_args, argc, argv )) {
	// Something was wrong.
	exit(1);
  }

  //////////////////////////////////////////////////////////////////////////////
  // Parse command line arguments.
  //////////////////////////////////////////////////////////////////////////////
  MPI_Init(&argc, &argv);
  MPI_Get_processor_name(my_name, &my_name_len);
  // Pull out the parameters so we don't need to type 'cmd_args.' all the time.
  num_dendrs = cmd_args.num_dendrs;
  num_comps = cmd_args.num_comps;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  printf("Simulating %d dendrites with %d compartments per dendrite.\n",
         num_dendrs, num_comps);
  //////////////////////////////////////////////////////////////////////////////
  // Create files where results will be stored.
  //////////////////////////////////////////////////////////////////////////////

  // Generate the graph and data file names.
  time_t t = time(NULL);
  struct tm *tmp = localtime(&t);
  strftime(time_str, 14, "%m%d%y_%H%M%S", tmp);

  // The resulting filenames will resemble
  //    pWWdXXcYY_MoDaYe_HoMiSe.xxx
  // where 'WW' is the number of processes, 'XX' is the number of dendrites,
  // 'YY' the number of compartments, and 'MoDaYe...' the time at which this
  // simulation was run.
  sprintf(graph_fname, "graphs/p1d%dc%d_%s.png",
          num_dendrs, num_comps, time_str);
  sprintf(data_fname, "data/p1d%dc%d_%s.dat",
          num_dendrs, num_comps, time_str);
  // printf("Time str: %s",time_str);
  // Verify that the graphs/ and data/ directories exist. Create them if they
  // don't.

  if (rank == 0)
  {
    struct stat stat_buf;
    stat("graphs", &stat_buf);
    if ((!S_ISDIR(stat_buf.st_mode)) && (mkdir("graphs", 0700) != 0))
    {
      fprintf(stderr, "Could not create 'graphs' directory!\n");
      exit(1);
    }

    stat("data", &stat_buf);
    if ((!S_ISDIR(stat_buf.st_mode)) && (mkdir("data", 0700) != 0))
    {
      fprintf(stderr, "Could not create 'data' directory!\n");
      exit(1);
    }

    // Verify that we can open files where results will be stored.
    if ((data_file = fopen(data_fname, "wb")) == NULL)
    {
      fprintf(stderr, "Can't open %s file!\n", data_fname);
      exit(1);
    }
    else
    {
      printf("\nData will be stored in %s\n", data_fname);
    }

    if (ISDEF_PLOT_PNG && (graph_file = fopen(graph_fname, "wb")) == NULL)
    {
      fprintf(stderr, "Can't open %s file!\n", graph_fname);
      exit(1);
    }
    else
    {
      printf("Graph will be stored in %s\n", graph_fname);
      fclose(graph_file);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Initialize simulation parameters.
  //////////////////////////////////////////////////////////////////////////////

  // The first compartment is a dummy and the last is connected to the soma.
  num_comps = num_comps + 2;

  // Initialize 'y' with precomputed values from the HH model.
  y[0] = VREST;
  y[1] = 0.037;
  y[2] = 0.0148;
  y[3] = 0.9959;

  // Setup parameters for the soma.
  soma_params[0] = 1.0 / (double)STEPS; // dt
  soma_params[1] = 0.0;                 // Direct current injection into soma is always zero.
  soma_params[2] = 0.0;                 // Dendritic current injected into soma. This is the
                                        // value that our simulation will update at each step.

  printf("\nIntegration step dt = %f\n", soma_params[0]);

  // Start the clock.
  ///!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

  gettimeofday(&start, NULL);

  // Initialize the potential of each dendrite compartment to the rest voltage.
  // if (rank == 0)
  // {
    dendr_volt = (double **)malloc(num_dendrs * sizeof(double *)); //here
    for (i = 0; i < num_dendrs; i++)
    {
      dendr_volt[i] = (double *)malloc(num_comps * sizeof(double));
      for (j = 0; j < num_comps; j++)
      {
        dendr_volt[i][j] = VREST;
      }
    }
  // }

  //////////////////////////////////////////////////////////////////////////////
  // Main computation.
  //////////////////////////////////////////////////////////////////////////////

  // Record the initial potential value in our results array.
  if (rank == 0)
  {
    res[0] = y[0];
  }
  // Loop over milliseconds.
  for (t_ms = 1; t_ms < COMPTIME; t_ms++)
  {

    // Loop over integration time steps in each millisecond.
    for (step = 0; step < STEPS; step++)
    {
      soma_params[2] = 0.0;
      double totalCurrent=0;
      MPI_Status status;
      int currTag=0,yTag=1;

      // Loop over all the dendrites.
      for (dendrite = 0; dendrite < num_dendrs; dendrite++)
      {
        if (rank-1 == dendrite % size&&rank!=0)
        {
          // This will update Vm in all compartments and will give a new injected
          // current value from last compartment into the soma.
            // printf("y check 1 %lf",y[0]);
            // exit(0);
            // if(dendrite>50){
            //   exit(0);
            // }
            current = dendriteStep(dendr_volt[dendrite],
                                     step + dendrite + 1,
                                     num_comps,
                                     soma_params[0],
                                     y[0]);
            totalCurrent+=current;
            // send(rank0)
            
          // Accumulate the current generated by the dendrite.
          // soma_params[2] += current;
        }
      }
        if(rank==0){
          double temp =0;
          for(int i=1;i<size;i++){
            MPI_Recv(&temp,1,MPI_DOUBLE,MPI_ANY_SOURCE ,currTag,MPI_COMM_WORLD,&status);
            soma_params[2]+=temp;
            }
        }
        else{
          MPI_Send(&totalCurrent,1,MPI_DOUBLE,0,currTag,MPI_COMM_WORLD);

        }
        // y0 needs to be sent out
        // as does soma_params
        // MPI_Send(buf, count, datatype, dest, tag, comm)
        // MPI_Recv(buf, count, datatype, source, tag, comm, *status)
        // int MPI_Bcast(void* buffer, int count, MPI_Datatype datatype, int root, MPI_Comm comm)
        //  MPI_Reduce(void* sendbuf, void* recvbuf, int count, MPI_Datatype datatype, MPI_Op op, int root, MPI_Commcomm)

        /**
         * Input:
  sendbuf - address of send buffer
  count - number of elements in send buffer (integer)
  datatype - data type of elements of send buffer (handle)
  op - reduce operation (handle) (user can create using MPI_Op_create
  or use predefined operations MPI_MAX, MPI_MIN, MPI_PROD, MPI_SUM,
  MPI_LAND, MPI_LOR, MPI_LXOR, MPI_BAND, MPI_BOR, MPI_BXOR,
  MPI_MAXLOC, MPI_MINLOC in place of MPI_Op op.
  root - rank of root process (integer)
  comm - communicator (handle)
  Output:
  recvbuf - address of receive buffer (significant only at root )
        */
      
        // MPI_Reduce(&current,&soma_params[2], 1, MPI_DOUBLE, MPI_SUM,0,MPI_COMM_WORLD);

      // Store previous HH model parameters.
      MPI_Barrier(MPI_COMM_WORLD);
      if(rank==0){
        y0[0] = y[0];      y0[1] = y[1];
        y0[2] = y[2];
        y0[3] = y[3];
          // printf("\n y check 2 %lf\n",y[0]);
      
      // This is the main HH computation. It updates the potential, Vm, of the
      // soma, injects current, and calculates action potential. Good stuff.
        soma(dydt, y, soma_params);
        rk4Step(y, y0, dydt, NUMVAR, soma_params, 1, soma);
        for(int i=1;i<size;i++)
          MPI_Send(&y[0],1,MPI_DOUBLE,i,yTag,MPI_COMM_WORLD);
        //send out new y0
      }else{
        MPI_Recv(&y[0],1,MPI_DOUBLE,0,yTag,MPI_COMM_WORLD,&status);
    }
    }
    if(rank==0){
      // Record the membrane potential of the soma at this simulation step.
      // Let's show where we are in terms of computation.
      printf("\r%02d ms", t_ms);
      fflush(stdout);

      res[t_ms] = y[0];
      // printf("res val: %lf at %d\n",y[0],t_ms);
      // exit(0);
    }
  }
  //////////////////////////////////////////////////////////////////////////////
  // Report results of computation.
  //////////////////////////////////////////////////////////////////////////////

  // Stop the clock, compute how long the program was running and report that
  // time.
  if (rank == 0)
  {
    gettimeofday(&stop, NULL);
    timersub(&stop, &start, &diff);
    exec_time = (double)(diff.tv_sec) + (double)(diff.tv_usec) * 0.000001;
    printf("\n\nExecution time: %f seconds.\n", exec_time);

    // Record the parameters for this simulation as well as data for gnuplot.
    fprintf(data_file,
            "# Vm for HH model. "
            "Simulation time: %d ms, Integration step: %f ms, "
            "Compartments: %d, Dendrites: %d, Execution time: %f s, "
            "Slave processes: %d\n",
            COMPTIME, soma_params[0], num_comps - 2, num_dendrs, exec_time,
            0);
    fprintf(data_file, "# X Y\n");

    for (t_ms = 0; t_ms < COMPTIME; t_ms++)
    {
      fprintf(data_file, "%d %f\n", t_ms, res[t_ms]);
    }
    fflush(data_file); // Flush and close the data file so that gnuplot will
    fclose(data_file); // see it.

    //////////////////////////////////////////////////////////////////////////////
    // Plot results if approriate macro was defined.
    //////////////////////////////////////////////////////////////////////////////
    if (ISDEF_PLOT_PNG || ISDEF_PLOT_SCREEN)
    {
      pinfo.sim_time = COMPTIME;
      pinfo.int_step = soma_params[0];
      pinfo.num_comps = num_comps - 2;
      pinfo.num_dendrs = num_dendrs;
      pinfo.exec_time = exec_time;
      pinfo.slaves = 0;
    }

    if (ISDEF_PLOT_PNG)
    {
      plotData(&pinfo, data_fname, graph_fname);
    }
    if (ISDEF_PLOT_SCREEN)
    {
      plotData(&pinfo, data_fname, NULL);
    }

    //////////////////////////////////////////////////////////////////////////////
    // Free up allocated memory.
    //////////////////////////////////////////////////////////////////////////////

    for (i = 0; i < num_dendrs; i++)
    {
      free(dendr_volt[i]);
    }
    free(dendr_volt);
  }
  MPI_Finalize();
  return 0;
}
