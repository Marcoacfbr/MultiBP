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

#include "../IManager.hpp"
#include "AbstractBlockPruning.hpp"
#include <algorithm>
#include <stdlib.h>
#include "../utils/AlignerUtils.hpp"
#include "../../common/Common.hpp"

//#define DUMP (0)
//#define SHARE (1)
//#define DEBUGM (1)

#define LIM 40050426
//#define LIM 5220960

extern int BestGlobal;
extern int jstart;
extern int jend;
extern FILE * dbabp;


AbstractBlockPruning::AbstractBlockPruning() {
	this->bestScore = -INF;
	this->grid = NULL;
	this->recurrenceType = SMITH_WATERMAN;
	this->score_params = NULL;
	this->max_i = 0;
	this->max_j = 0;
}

AbstractBlockPruning::~AbstractBlockPruning() {

}

void AbstractBlockPruning::updateBestScore(int score) {
	if (this->bestScore < score) {
		this->bestScore = score;
	}
}

const Grid* AbstractBlockPruning::getGrid() const {
	return grid;
}

void AbstractBlockPruning::setGrid(const Grid* grid) {
	if (this->grid != NULL) {
		finalize();
	}
	this->grid = grid;
	this->bestScore = -INF;
	if (this->grid != NULL) {
		initialize();
	}
}

void AbstractBlockPruning::setSuperPartition(Partition superPartition) {
	this->max_i = superPartition.getI1();
	this->max_j = superPartition.getJ1();
}

void AbstractBlockPruning::setScoreParams(const score_params_t* score_params) {
	this->score_params = score_params;
}

bool AbstractBlockPruning::isBlockPrunable(int bx, int by, int score) {
	if (grid == NULL) return false;
	int i0;
	int j0;
	int i1;
	int j1;
	int adjustment = grid->getBlockAdjustment(bx,by);

   	grid->getBlockPosition(bx, by, &i0, &j0, &i1, &j1);
	if (i0 == -1) {
		return true;
	}


	int distI = max_i - i0;
	int distJ = max_j - j0;

	int distMin = (distI<distJ)?distI:distJ;
	int inc = distMin*score_params->match;
	int dec = 0;



	if (recurrenceType == NEEDLEMAN_WUNSCH) {
		int gaps;

		gaps = abs(distJ-distI) - std::max(j1-j0, i1-i0);
		if (gaps > 0) {
			inc -= score_params->gap_open + gaps*score_params->gap_ext;
		}

		// FIXME - must consider alignment edge instead of recurrenceType
		dec -= distMin*score_params->mismatch;
		gaps = abs(distJ-distI) + std::max(j1-j0, i1-i0);
		dec += score_params->gap_open + gaps*score_params->gap_ext;
	}

	updateBestScore(score - dec);

	int max = score + inc + adjustment;
        
        if ((max <= bestScore) && (DEBUGM))
          fprintf(dbabp, "diagonal:XXX ** max_i: %d max_j: %d ** bx:%d  by:%d ** i0:%d  j0:%d **  %d+%d<%d\n",  max_i, max_j, bx, by, i0, j0, score,inc, bestScore);
        


	return (max <= bestScore);
}

bool AbstractBlockPruning::isBlockPrunableMulti(int bx, int by, int score, int diagonal) {
	if (grid == NULL) return false;
	int i0;
	int j0;
	int i1;
	int j1;
	int adjustment = grid->getBlockAdjustment(bx,by);
        
        //FILE * fout;
        //fout = fopen ("err.txt", "at");
        

	int best;
	pthread_mutex_t lock;

   	grid->getBlockPositionMulti(bx, by, &i0, &j0, &i1, &j1);
	if (i0 == -1) {
		return true;
	}

	max_j = grid->seq1_size;

	int distI = max_i - i0;
	int distJ = max_j - j0;

	int distMin = (distI<distJ)?distI:distJ;
        int minvalue = ((max_i<max_j)?max_i:max_j)*score_params->match;
	int inc = distMin*score_params->match;
	int dec = 0;

	//fprintf(stderr, "--isBlockPrunable? (%d,%d) (%d,%d) %d+%d<%d\n", bx, by, i0, j0, score,inc, bestScore);

        //fclose(fout);

	if (recurrenceType == NEEDLEMAN_WUNSCH) {
		int gaps;

		gaps = abs(distJ-distI) - std::max(j1-j0, i1-i0);
		if (gaps > 0) {
			inc -= score_params->gap_open + gaps*score_params->gap_ext;
		}

		// FIXME - must consider alignment edge instead of recurrenceType
		dec -= distMin*score_params->mismatch;
		gaps = abs(distJ-distI) + std::max(j1-j0, i1-i0);
		dec += score_params->gap_open + gaps*score_params->gap_ext;
	}

	updateBestScore(score - dec);

	best = bestScore;

        if (SHARE && (diagonal%DIAG==0) && (BestGlobal < minvalue)) {
  		pthread_mutexattr_t mutexattr;
		pthread_mutexattr_init(&mutexattr);
		pthread_mutexattr_settype(&mutexattr, PTHREAD_MUTEX_RECURSIVE_NP);
		pthread_mutex_init(&lock, &mutexattr);
		pthread_mutex_lock(&lock);
                //if (BestGlobal > LIM)
                //    printf("\n best: %d ** bestScore: %d ** BestGlobal: %d ** diagonal: %d  ** max_i: %d max_j: %d ** bx:%d  by:%d ** i0:%d  j0:%d **  %d+%d<%d\n",  best, bestScore, BestGlobal, diagonal, max_i, max_j, bx, by, i0, j0, score,inc, best);
		if (BestGlobal >= best) {
			best = BestGlobal;
                        // if (BestGlobal > LIM)
                        //   printf("\n 1111 best: %d ** bestScore: %d ** BestGlobal: %d ** diagonal: %d  ** max_i: %d max_j: %d ** bx:%d  by:%d ** i0:%d  j0:%d **  %d+%d<%d\n",  best, bestScore, BestGlobal, diagonal, max_i, max_j, bx, by, i0, j0, score,inc, best);
                }                        
		else { 
			BestGlobal = best;
                        // if (BestGlobal > LIM)
                        //   printf("\n 2222 best: %d ** bestScore: %d ** BestGlobal: %d ** diagonal: %d  ** max_i: %d max_j: %d ** bx:%d  by:%d ** i0:%d  j0:%d **  %d+%d<%d\n",  best, bestScore, BestGlobal, diagonal, max_i, max_j, bx, by, i0, j0, score,inc, best);
                }
		pthread_mutex_unlock(&lock);
                updateBestScore(best);

        }

        int max = score + inc + adjustment;

        if (DUMP) {
          if (max <= best) {
            printf("%d %d\n", i0, j0);
            printf("%d %d\n", i1, j1);
          }
       }

       if ((max <= best) && (DEBUGM))
          fprintf(dbabp, "diagonal: %d  ** max_i: %d max_j: %d ** bx:%d  by:%d ** i0:%d  j0:%d **  %d+%d<%d\n",  diagonal, max_i, max_j, bx, by, i0, j0, score,inc, best);
          //printf("\n %d %d %d %d %d %d %d %d %d", max_i, max_j, bx, by, i0, j0, score,inc, best);

	//fprintf(stderr, "isBlockPrunable(%2d,%2d, [%d/%d]) D:%d +%d-%d - Max: %d\n", bx, by, score, bestScore, distMin, inc, dec, max);
	//return (max <= bestScore);
	return (max <= best);
}


void AbstractBlockPruning::setRecurrenceType(int recurrenceType) {
	this->recurrenceType = recurrenceType;
}

int AbstractBlockPruning::getRecurrenceType() const {
	return recurrenceType;
}
