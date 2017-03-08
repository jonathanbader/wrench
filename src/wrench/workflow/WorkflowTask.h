/**
 *  @file    WorkflowTask.h
 *  @author  Henri Casanova
 *  @date    2/21/2017
 *  @version 1.0
 *
 *  @brief WRENCH::WorkflowTask class implementation
 *
 *  @section DESCRIPTION
 *
 *  The WRENCH::WorkflowTask class represents a computational
 *  task in a WRENCH:Workflow.
 *
 */
#ifndef WRENCH_WORKFLOWTASK_H
#define WRENCH_WORKFLOWTASK_H

#include <map>
#include <lemon/list_graph.h>

#include "WorkflowFile.h"


using namespace lemon;

namespace WRENCH {

		// Forward reference
		class Workflow;


		/* Task meta-data class */
		class WorkflowTask {

				// Workflow class must be a friend so as to access the private constructor, etc.
				friend class Workflow;

		public:
				/* Task-state enum */
				enum State {
						READY,
						NOT_READY,
						SCHEDULED,
						RUNNING,
						COMPLETED,
						FAILED
				};

		public:
				std::string id;
				double flops;
				int number_of_processors;		// currently vague: cores? nodes?
				double submit_date = -1.0;
				double start_date = -1.0;
				double end_date = -1.0;

		private:
				State state;
				Workflow *workflow; 	// Containing workflow
				ListDigraph *DAG; 	// Containing workflow
				ListDigraph::Node DAG_node; // pointer to the underlying DAG node
				std::map<std::string, WorkflowFile *> output_files;  // List of output files
				std::map<std::string, WorkflowFile *> input_files;   // List of input files


		private:
				// Private constructor
				WorkflowTask(const std::string id, const double t, const int n);
				void addInputFile(WorkflowFile *);
				void addOutputFile(WorkflowFile *);
				void addFileToMap(std::map<std::string, WorkflowFile*> map, WorkflowFile *f);
				void setState(WorkflowTask::State);


		public:
				int getNumberOfChildren();
				int getNumberOfParents();
				WorkflowTask::State getState();
				void setScheduled();
				void setRunning();
				void setCompleted();

		};

};


#endif //WRENCH_WORKFLOWTASK_H
