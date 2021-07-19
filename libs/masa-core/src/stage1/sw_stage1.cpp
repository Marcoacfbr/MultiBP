/*******************************************************************************
 *
 * Copyright (c) 2010-2015   Edans Sandes
 *
 * This file is part of MASA-Core.
 * 
 * MASA-Core is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * MASA-Core is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with MASA-Core.  If not, see <http://www.gnu.org/licenses/>.
 *
 ******************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "../common/Common.hpp"
#include "../common/extern.hpp"
#include "../common/io/FileCellsReader.hpp"
#include "../common/io/FileCellsWriter.hpp"
#include "../common/io/InitialCellsReader.hpp"
#include "../common/io/BufferedCellsReader.hpp"
#include "../common/io/URLCellsReader.hpp"
#include "../common/io/BufferedCellsWriter.hpp"
#include "../common/io/URLCellsWriter.hpp"
#include "../common/io/TeeCellsReader.hpp"
#include "../common/io/SplitCellsReader.hpp"

#include <map>
using namespace std;


#define DEBUG	(0)
#define DEBUGM   (0)
#define SYNCTIME (5)
//#define SHARE (0)

/* Stores a list with the best score and some suboptimal scores */
static BestScoreList* bestScoreList;

/* The current aligner object */
static IAligner* aligner;

/* The status file */
static Status* status;

/* The object where the special rows are saved */
static SpecialRowsPartition* sraPartition = NULL;

/* Indicates if the best score should be updated */
//static bool processBestScore;

/** Block files for storing block results */
static BlocksFile* blocksFile;

extern int BestGlobal;
extern int dynamic;
extern int lastit;
extern string wdir;
extern FILE * dbabp;
extern FILE * dbbpd;
extern FILE * dbsta;
extern FILE * dbdyn;
//extern double pruningtime;

Job * jobGlobal; 
pthread_mutex_t lock;
pthread_mutexattr_t mutexattr;

/**
 * Decides if the initial best score should be zero or -INF.
 */
static score_t getInitialBestScore(int alignment_start, int alignment_end) {
	int may_start_at_corner_00 = alignment_start == AT_ANYWHERE
			|| alignment_start == AT_SEQUENCE_1
			|| alignment_start == AT_SEQUENCE_2
			|| alignment_start == AT_SEQUENCE_1_OR_2
			|| alignment_start == AT_SEQUENCE_1_AND_2;
	int may_start_at_corner_01 = alignment_start == AT_ANYWHERE
			|| alignment_start == AT_SEQUENCE_1
			|| alignment_start == AT_SEQUENCE_1_OR_2;
	int may_start_at_corner_10 = alignment_start == AT_ANYWHERE
			|| alignment_start == AT_SEQUENCE_2
			|| alignment_start == AT_SEQUENCE_1_OR_2;
	int may_start_at_corner_11 = alignment_start == AT_ANYWHERE;

	int may_end_at_corner_00 = alignment_end == AT_ANYWHERE;
	int may_end_at_corner_01 = alignment_end == AT_ANYWHERE
			|| alignment_end == AT_SEQUENCE_2
			|| alignment_end == AT_SEQUENCE_1_OR_2;
	int may_end_at_corner_10 = alignment_end == AT_ANYWHERE
			|| alignment_end == AT_SEQUENCE_1
			|| alignment_end == AT_SEQUENCE_1_OR_2;
	int may_end_at_corner_11 = alignment_end == AT_ANYWHERE
			|| alignment_end == AT_SEQUENCE_1
			|| alignment_end == AT_SEQUENCE_2
			|| alignment_end == AT_SEQUENCE_1_OR_2
			|| alignment_end == AT_SEQUENCE_1_AND_2;

	score_t best;
	best.i = -1;
	best.j = -1;
	if ((may_start_at_corner_00 && may_end_at_corner_00)
			|| (may_start_at_corner_01 && may_end_at_corner_01)
			|| (may_start_at_corner_10 && may_end_at_corner_10)
			|| (may_start_at_corner_11 && may_end_at_corner_11)) {
		best.score = 0;
	} else {
		best.score = -INF;
	}
	return best;
}

/**
 * Saves the status to the status file and prints the current best score
 * to the stderr.
 */
static void logStatus(float t) {
	// FIXME This method is not thread safe!!!!
	if (sraPartition != NULL) {
		status->setLastSpecialRow(sraPartition->getLastRowId());
	}
	status->save(); // FIXME This method is not thread safe!!!!

	int hour = (int) (t / 3600);
	int min = (int) (t - hour * 3600) / 60;
	int sec = (int) (t - hour * 3600 - min * 60);
	score_t bestScore = bestScoreList->getBestScore();
	fprintf(stderr, "(%dh%02dm%02ds) best:(%d,%d,%d) %s\n",
			hour, min, sec,
			bestScore.i, bestScore.j, bestScore.score,
			aligner->getProgressString());
}

static void getBorderCells(Job* job, SpecialRowsPartition* sraPartition,
		SeekableCellsReader* &firstRow, SeekableCellsReader* &firstColumn, CellsWriter* &lastRow, CellsWriter* &lastColumn) {
	const score_params_t* score_params = job->aligner->getScoreParameters();

	int startOffset0 = job->getSequence(0)->getTrimStart() - job->getSequence(0)->getModifiers()->getTrimStart();
	int startOffset1 = job->getSequence(1)->getTrimStart() - job->getSequence(1)->getModifiers()->getTrimStart();

	switch (job->alignment_start) {
		case AT_ANYWHERE:
			firstRow = new InitialCellsReader(startOffset1);
			firstColumn = new InitialCellsReader(startOffset0);
			break;
		case AT_SEQUENCE_1:
			firstRow = new InitialCellsReader(startOffset1);
			firstColumn = new InitialCellsReader(score_params->gap_open, score_params->gap_ext, startOffset0);
			break;
		case AT_SEQUENCE_2:
			firstRow = new InitialCellsReader(score_params->gap_open, score_params->gap_ext, startOffset1);
			firstColumn = new InitialCellsReader(startOffset0);
			break;
		case AT_SEQUENCE_1_OR_2:
			firstRow = new InitialCellsReader(startOffset1);
			firstColumn = new InitialCellsReader(startOffset0);
			break;
		case AT_SEQUENCE_1_AND_2:
			firstRow = new InitialCellsReader(score_params->gap_open, score_params->gap_ext, startOffset1);
			firstColumn = new InitialCellsReader(score_params->gap_open, score_params->gap_ext, startOffset0);
			break;
		default:
			fprintf(stderr, "Unknown alignment start type.\n");
			exit(1);
	}

//	if (job->getSequence(1)->getTrimStart() != job->getSequence(1)->getModifiers()->getTrimStart()) {
//		//firstRow = new SplitCellsReader(firstRow, job->getSequence(1)->getTrimStart() - job->getSequence(1)->getModifiers()->getTrimStart(), true);
//		firstRow->seek(job->getSequence(1)->getTrimStart() - job->getSequence(1)->getModifiers()->getTrimStart());
//	}

	if (job->flush_column_url.size() > 0) {
		CellsWriter* writer = new URLCellsWriter(job->flush_column_url);
		BufferedCellsWriter* tmp = new BufferedCellsWriter(writer, job->getBufferLimit());
		tmp->setLogFile(job->outputBufferLogFile, 10.0f);
		lastColumn = tmp;
	}

	if (job->load_column_url.size() > 0) {
		if (firstColumn != NULL) delete firstColumn;

		CellsReader* reader = new URLCellsReader(job->load_column_url);
		int limit = job->getBufferLimit();
		if (job->getPoolWaitId() >= 0) {
			limit = job->getSequence(0)->getLen();
		}
		BufferedCellsReader* tmp = new BufferedCellsReader(reader, limit);
		tmp->setLogFile(job->inputBufferLogFile, 10.0f);
		if (DEBUG) printf("Creating first column reader\n");
		firstColumn = tmp;
		// XXXXXXXXXXXXXX TODO XXXXXXXXXXXXXXX
		/*if (sraPartition != NULL) {
			string filename = sraPartition->getFirstColumnWFilename();
			firstColumn = new TeeCellsReader(tmp, filename);
			if (DEBUG) printf("Creating TeeCellsReader\n");
		} else {
			firstColumn = tmp;
		}*/
		// XXXXXXXXXXXXXX TODO XXXXXXXXXXXXXXX
	}
}

void alignPartition(SpecialRowsPartition* sraPartition, int ev_prepare, int ev_init,
		int ev_align, Job* job, AlignerManager* sw, Timer& timer,
		FILE* stats) {
	int i0 = sraPartition->getI0();
	int j0 = sraPartition->getJ0();
	int i1 = sraPartition->getI1();
	int j1 = sraPartition->getJ1();

	::sraPartition = sraPartition;
	sw->setSpecialRowsPartition(sraPartition);

	if (job->getSRALimit() > 0) {
		if (sraPartition->getLastRowId() == i1) {
			printf("Stage 1 was already executed\n");
			i0 = i1;
		} else if (sraPartition->getLastRowId() != i0) {
			i0 = sraPartition->continueFromLastRow();
		}
	}
	timer.eventRecord(ev_prepare);
	bool block_pruning;
	if (job->alignment_end == AT_ANYWHERE) {
		block_pruning = job->block_pruning;
	} else {
		block_pruning = false;
	}
	sw->setBlockPruning(block_pruning);
	timer.eventRecord(ev_init);
	Partition partition(i0, j0, i1, j1);

    /*bool split;

    if ((j0 == 0) && (j1 != job->getSequence(1)->getLen()))
    	split = true;
    else
    	split = false;

    sw->setmustSplit(split);*/

	partition.split = job->split;
	partition.seq1_size = job->seq1_size;

	//printf("\n\n\n\n ****  Partition: %d **** \n\n\n", partition.seq1_size);


	//printf("\n\n\n\n ****  Partition: i0: %d, j0: %d, i1: %d, j1: %d --- S1: %d **** \n", i0,j0,i1,j1, job->getSequence(1)->getLen());
	sw->alignPartition(partition, START_TYPE_MATCH);
	timer.eventRecord(ev_align);

	if (job->getAlignerPool() != NULL) {
		score_t lastColumnScore = sw->getBestScoreLastColumn();
		if (lastColumnScore.score > job->getAlignerPool()->getBestNodeScore().score) {
			job->getAlignerPool()->setBestNodeScore(lastColumnScore);
		}
	}
}

void * shareScore (void * x ) {
   
   int threadrec;

   while (1) {
      jobGlobal->getAlignerPool()->dispatchBestScore(BestGlobal);
      threadrec = jobGlobal->getAlignerPool()->receiveBestScore();
      //if (DEBUGM) fprintf (dbsta, "BestGlobal: %d *** threadrc: %d  \n", BestGlobal, threadrec);
      pthread_mutex_lock(&lock);
      if (threadrec >  BestGlobal)
        BestGlobal = threadrec;
      pthread_mutex_unlock(&lock);
      //printf ("\n\n --- splitstep: %d - dynamic: %d \n", splitstep, dynamic);
      if (dynamic != 0) {
       if (splitstep % dynamic == 0)
          jobGlobal->getAlignerPool()->dispatchBestScoreDyn(BestGlobal);
       if (splitstep % dynamic == 1) { 
      	threadrec = jobGlobal->getAlignerPool()->receiveBestScoreDyn();
      	//if (DEBUGM) fprintf (dbsta, "BestGlobal: %d *** threadrc: %d  \n", BestGlobal, threadrec);
      	pthread_mutex_lock(&lock);
      	if (threadrec >  BestGlobal)
             BestGlobal = threadrec;
      	pthread_mutex_unlock(&lock);
       }
      }
      
     
      sleep(SYNCTIME);
   }
}



/**
 * Stage 1 entry point.
 *
 * @return the number of best scores stored in the work directory.
 */
int stage1(Job* job) {
	FILE* stats = job->fopenStatistics(STAGE_1, 0);
	job->getAlignmentParams()->printParams(stats);
	fflush(stats);

       string dir;
       string filename;
       dir =  job->getWorkPath();
       wdir =  job->getWorkPath();


    if (DEBUGM) {
       //printf ("\n\n *** path: %s *** \n\n", dir.c_str());
       filename = dir + "/DEBUGM_ABP.txt";
       dbabp = fopen(filename.c_str(),"wt");
       filename = dir + "/DEBUGM_BPD.txt";
       dbbpd = fopen(filename.c_str(),"wt");
       //filename = dir + "/DEBUGM_STA.txt";
       //dbsta = fopen(filename.c_str(),"wt");
    }

    /* if (dynamic != 0) 
       if (splitstep%dynamic == 1) {
          filename = dir + "/dyn.txt";
          //dbdyn = fopen(filename.c_str(),"wt");
       }
   */

    BestGlobal = 0;
    pthread_t thr;
    pthread_mutexattr_init(&mutexattr);
    pthread_mutexattr_settype(&mutexattr, PTHREAD_MUTEX_RECURSIVE_NP);
    pthread_mutex_init(&lock, &mutexattr);
    if (SHARE) { 
        if ((job->split) && (job->block_pruning)) {
            jobGlobal = job;
    	    pthread_create(&thr, NULL, shareScore, (void *) NULL);
        }
    }

	Sequence* seq_vertical = new Sequence(job->getAlignmentParams()->getSequence(0));
	Sequence* seq_horizontal = new Sequence(job->getAlignmentParams()->getSequence(1));

	aligner = job->aligner;
	const score_params_t* score_params = aligner->getScoreParameters();
	AlignerManager* sw = new AlignerManager(aligner);

	if (job->split == 1)
		sw->setmustSplit(true);

	aligner->clearStatistics();

	Timer timer;
	
	int ev_prepare = timer.createEvent("PREPARE");
	int ev_init = timer.createEvent("INIT");
	int ev_align = timer.createEvent("ALIGN");
	int ev_end = timer.createEvent("END");
	
	timer.init();

//	int i0 = seq_vertical->getModifiers()->getTrimStart()-1;
//	int i1 = seq_vertical->getModifiers()->getTrimEnd();
//	int j0 = seq_horizontal->getModifiers()->getTrimStart()-1;
//	int j1 = seq_horizontal->getModifiers()->getTrimEnd();

	int ii0 = seq_vertical->getModifiers()->getTrimStart()-1;
	int ii1 = seq_vertical->getModifiers()->getTrimEnd();
	int jj0 = seq_horizontal->getModifiers()->getTrimStart()-1;
	int jj1 = seq_horizontal->getModifiers()->getTrimEnd();

	Partition superPartition = Partition(ii0, jj0, ii1, jj1);
	sw->setSuperPartition(superPartition);

	int i0 = seq_vertical->getTrimStart()-1;
	int i1 = seq_vertical->getTrimEnd();
	int j0 = seq_horizontal->getTrimStart()-1;
	int j1 = seq_horizontal->getTrimEnd();
	int seq0_len = i1-i0;
	int seq1_len = j1-j0;
	

	if (DEBUG) {
		printf("seq0: %.60s...\n", seq_vertical->getData());
		printf("seq1: %.60s...\n", seq_horizontal->getData());
		printf("Partition: (%d,%d,%d,%d)\n", i0, j0, i1, j1);
	}
	if (job->getSRALimit() > 0) {
		int flush_interval = job->getFlushInterval(0);
		long long special_lines_count = (seq0_len / flush_interval);
		fprintf(stats, "Flush Interval: %d\n", flush_interval);
		fprintf(stats, "Special lines: %lld\n", special_lines_count);
		fprintf(stats, "Total size: %lld\n",
				special_lines_count * (seq1_len+1) * sizeof(cell_t));
		fflush(stats);
		sw->setSpecialRowInterval(flush_interval);
	} else {
		sw->setSpecialRowInterval(0);
	}
	fprintf(stats, "Flush limit: %lld\n", job->getSRALimit());

	blocksFile = NULL;

	if (job->dump_blocks) {
		blocksFile = new BlocksFile(job->dump_pruning_text_filename);
		//sw->setProcessScoreFunction(processBestScoreFunction);
		sw->setBlocksFile(blocksFile);
	}
	RecurrentTimer* logger = new RecurrentTimer(logStatus);


	if (job->alignment_start == AT_ANYWHERE) {
		sw->setRecurrenceType(SMITH_WATERMAN);
	} else {
		sw->setRecurrenceType(NEEDLEMAN_WUNSCH);
	}



	score_t minScore = getInitialBestScore(job->alignment_start, job->alignment_end);

	bestScoreList = new BestScoreList(job->max_alignments, minScore.score, seq0_len, seq1_len, score_params);

	status = new Status(job->getWorkPath(), bestScoreList);
	if (!status->isEmpty()) {
		//bestScore = bestScoreList->getBestScore();
	} else {
		status->setCurrentStage(STAGE_1);
		status->save();
	}
	//bestScoreList->add(bestScore.i, bestScore.j, bestScore.score);
	//printf("Best Init: %d,%d,%d\n", bestScore.j, bestScore.i, bestScore.score);

	SpecialRowsArea* sra = NULL;

	SeekableCellsReader* firstRow = NULL;
	SeekableCellsReader* firstColumn = NULL;
	CellsWriter* lastRow = NULL;
	CellsWriter* lastColumn = NULL;

	getBorderCells(job, sraPartition, firstRow, firstColumn, lastRow,
			lastColumn);

	if (job->getAlignerPool() != NULL) {
		// Forces to wait until a given partition is finished
		int waitId = job->getPoolWaitId();
		if (waitId >= 0) {
			job->getAlignerPool()->waitId(waitId);
		}
	}


	sra = job->getSpecialRowsArea(STAGE_1, 0);


	aligner_capabilities_t capabilities = aligner->getCapabilities();
	int ni = 1;
	int nj = 1;
	//capabilities.maximum_seq0_len = 5000000;
	//capabilities.maximum_seq1_len = 5000000;
	if (capabilities.maximum_seq0_len != 0 && capabilities.maximum_seq0_len < i1-i0) {
		ni = (i1-i0-1)/capabilities.maximum_seq0_len + 1;
	}
	if (capabilities.maximum_seq1_len != 0 && capabilities.maximum_seq1_len < j1-j0) {
		nj = (j1-j0-1)/capabilities.maximum_seq1_len + 1;
	}
	printf("---%d,%d   %d x %d\n", capabilities.maximum_seq0_len, capabilities.maximum_seq1_len, ni, nj);
	sra->createSplittedPartitions(i0, j0, i1, j1, ni, nj,
			firstRow, firstColumn, lastRow,	lastColumn);

        lastit = 0;
	logger->start(2.0);
	vector<SpecialRowsPartition*> sortedPartitions = sra->getSortedPartitions();
	for(vector<SpecialRowsPartition*>::iterator it = sortedPartitions.begin(); it != sortedPartitions.end(); ++it) {
		//printf(">>>>> %d,%d,%d,%d\n", (*it)->getI0(), (*it)->getJ0(), (*it)->getI1(), (*it)->getJ1());
                if (std::next(it) == sortedPartitions.end())
			lastit = 1;
		sw->setBestScoreList(bestScoreList, job->alignment_end);
		if (job->alignment_end == AT_SEQUENCE_1 && (*it)->getI1() != ii1) {
			sw->setBestScoreList(bestScoreList, AT_NOWHERE);
		} else if (job->alignment_end == AT_SEQUENCE_2 && (*it)->getJ1() != jj1) {
			sw->setBestScoreList(bestScoreList, AT_NOWHERE);
		} else if (job->alignment_end == AT_SEQUENCE_1_AND_2 && ((*it)->getI1() != ii1 || (*it)->getJ1() != jj1)) {
			sw->setBestScoreList(bestScoreList, AT_NOWHERE);
		} else if (job->alignment_end == AT_SEQUENCE_1_OR_2) {
			if ((*it)->getI1() == ii1 && (*it)->getJ1() != jj1) {
				sw->setBestScoreList(bestScoreList, AT_SEQUENCE_1);
			} else if ((*it)->getI1() != ii1 && (*it)->getJ1() == jj1) {
				sw->setBestScoreList(bestScoreList, AT_SEQUENCE_2);
			} else if ((*it)->getI1() != ii1 || (*it)->getJ1() != jj1) {
				sw->setBestScoreList(bestScoreList, AT_NOWHERE);
			}
		}
		sw->setSequences(seq_vertical, seq_horizontal, (*it)->getI0(), (*it)->getJ0(), (*it)->getI1(), (*it)->getJ1(), stats);
		alignPartition(*it, ev_prepare, ev_init, ev_align, job,
				sw, timer, stats);
		sw->unsetSequences();

		logger->logNow();
	}
	logger->stop();
	delete logger;

         if ((SHARE) && (dynamic != 0))
            if ((job->split) && (job->block_pruning))
                pthread_cancel(thr);

    if (DEBUGM) {
       fflush(dbabp);
       fclose(dbabp);
       fflush(dbbpd);
       fclose(dbbpd);
       //fflush(dbsta);
       //fclose(dbsta);
    }
    
    if (dynamic != 0) {
            string filenamedyn = wdir + "/dynend.txt";
            dbdyn = fopen(filenamedyn.c_str(),"wt");
            fprintf(dbdyn,"END");
            fclose (dbdyn);
    }


	if (blocksFile != NULL) {
		blocksFile->close();
		delete blocksFile;
		blocksFile = NULL;
	}
	//	if (firstColumn != NULL) {
	//		delete firstColumn;
	//	}
	printf(">>>>> Deleting lastColumn: %p\n", lastColumn);
	if (lastColumn != NULL) {
		delete lastColumn;
	}

	if (job->getAlignerPool() != NULL) {
		if (!job->getAlignerPool()->isFirstNode()) {
			score_t score = job->getAlignerPool()->receiveScore();
			bestScoreList->add(score.i, score.j, score.score);
		}
	}
	score_t best_score = bestScoreList->getBestScore();

	if (best_score.score > job->bestglobalscore.score) {
		job->bestglobalscore.score = best_score.score;
		job->bestglobalscore.i = best_score.i;
		job->bestglobalscore.j = best_score.j;
		job->bestglobalscore.node = job->node;
	}

	fprintf(stats, "======= Execution Status =======\n");
	fprintf(stats, "   Best Score: %d\n", best_score.score);
	fprintf(stats, "Best Position: (%d,%d)\n", best_score.i, best_score.j);
	status->setCurrentStage(STAGE_2);
	status->save();
	delete status;





	timer.eventRecord(ev_end);
	fprintf(stats, "Stage1 times:\n");
	float diff = timer.printStatistics(stats);
	// TODO different number of total_cells when we continue previous execution
	long long total_cells = ((long long) (((i1 - i0)))) * (j1 - j0);
	fprintf(stats, "        Total: %.4f\n", diff);
	fprintf(stats, "        Cells: %.4e (%.4e)\n", (double) (total_cells),
			(double) (aligner->getProcessedCells()));
	fprintf(stats, "        MCUPS: %.4f\n", total_cells/1000000.0f/(diff/1000.0f));


	aligner->printStatistics(stats);
	delete sw;
	delete seq_vertical;
	delete seq_horizontal;
	//delete specialRowWriter;
	
	fclose(stats);
//
        //printf ("\n\n Pruning time: %f \n\n", pruningtime);

	if (job->getAlignerPool() != NULL) {
		crosspoint_t c;
		if (!job->getAlignerPool()->isLastNode()) {
			job->getAlignerPool()->dispatchScore(best_score);
			best_score = job->getAlignerPool()->getBestNodeScore();
		}
		score_t node_score = best_score;//job->getAlignerPool()->getBestNodeScore();
		c.i = node_score.i;
		c.j = node_score.j;
		c.score = node_score.score;
		c.type = TYPE_MATCH;

		CrosspointsFile* crosspointsFile = new CrosspointsFile(
				job->getCrosspointFile(STAGE_1, 0));
		crosspointsFile->setAutoSave();
		crosspointsFile->write(c);
		crosspointsFile->close();
		delete crosspointsFile;

		return 1;
	} else {
		int count = 0;
		for (set<score_t>::iterator it = bestScoreList->begin();
				it != bestScoreList->end(); it++) {
			CrosspointsFile* crosspointsFile = new CrosspointsFile(
					job->getCrosspointFile(STAGE_1, count++));
			crosspointsFile->setAutoSave();
			crosspointsFile->write(it->i, it->j, it->score, 0);
			crosspointsFile->close();
			delete crosspointsFile;
		}

		return count;
	}
}

