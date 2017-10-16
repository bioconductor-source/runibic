/***
Copyright (c) 2017 Patryk Orzechowski, Artur Pańszczyk

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
***/


#include <iostream>
#include <Rcpp.h>
#include <cstdlib>
#include <omp.h>
#include <vector>
#include <algorithm>
#include <utility>
#include <set>
#include <iterator>
#include <functional>
#include "GlobalDefs.h"

using namespace std;
using namespace Rcpp;

Params gParameters;

// [[Rcpp::plugins(cpp11)]]
// Enable OpenMP (exclude macOS)
// [[Rcpp::plugins(openmp)]]



//' Set the parameters
//'
//' Runibic function for setting parameters
//'
//' @param t consistency level of the block (0.5-1.0] 
//' @param q a double value for quantile discretization
//' @param f filtering overlapping blocks, default 1(do not remove any blocks)
//' @param nbic maximum number of biclusters in output
//' @param div number of ranks as which we treat the up(down)-regulated value: default: 0==ncol(x)
//' @return NULL (an empty value)
//'
//' @examples
//' runibic_params(0.85,0,1,100,0)
//'
// [[Rcpp::export]]
void runibic_params(double t = 0.85,double q = 0,double f = 1, int nbic = 100,int div = 0)
{
  gParameters.Tolerance=t;
  gParameters.Quantile = q;
  gParameters.Filter = f;
  gParameters.RptBlock = nbic;
  gParameters.SchBlock = 2*gParameters.RptBlock;
  gParameters.Divided = div;
}


//' Discretize an input matrix
//'
//' This function discretizes the input matrix
//'
//' @param x a numeric matrix
//' @return a discretized matrix containing integers only
//'
//' @examples
//' A=replicate(10, rnorm(20))
//' discretize(A)
//'
// [[Rcpp::export]]
Rcpp::IntegerMatrix discretize(Rcpp::NumericMatrix x) {
  IntegerMatrix y(x.nrow(),x.ncol());

  gParameters.InitOptions(x.nrow(),x.ncol());

  if(gParameters.Quantile >=0.5){
    for(auto iRow = 0; iRow < x.nrow(); iRow++){
      NumericVector rowData = x(iRow,_);
      rowData.sort();

      for(auto iCol = 0; iCol < x.ncol(); iCol++){
        double dSpace = 1.0 / gParameters.Divided;
        for(auto ind=0; ind < gParameters.Divided; ind++){
          if(x(iRow,iCol) >= calculateQuantile(rowData, x.ncol(), 1.0 - dSpace * (ind+1))){
            y(iRow,iCol) = ind+1;
            break;
          }
        }
      }
    }
  }
  else{
    for(auto iRow = 0; iRow < x.nrow(); iRow++){
      NumericVector rowData = x(iRow,_);
      sort(rowData.begin(), rowData.end());

      double partOne = calculateQuantile(rowData,x.ncol(),1-gParameters.Quantile);
      double partTwo = calculateQuantile(rowData,x.ncol(),gParameters.Quantile);
      double partThree = calculateQuantile(rowData, x.ncol(), 0.5);
      double upperLimit, lowerLimit;

      if((partOne-partThree) >= (partThree - partTwo)){
        upperLimit = 2*partThree - partTwo;
        lowerLimit = partTwo;
      }
      else{
        upperLimit = partOne;
        lowerLimit = 2*partThree - partOne;
      }
      NumericVector upperPart, lowerPart;
      upperPart = rowData[rowData > upperLimit];
      lowerPart = rowData[rowData < lowerLimit];
      for(auto iCol = 0; iCol < x.ncol(); iCol++){
        double dSpace = 1.0 / gParameters.Divided;
        for(auto ind=0; ind < gParameters.Divided; ind++){
          if(lowerPart.size() > 0 && x(iRow,iCol) <= calculateQuantile(lowerPart, lowerPart.size(), dSpace * (ind))){
            y(iRow,iCol) = -ind-1;
            break;
          }
          if(upperPart.size() > 0 && x(iRow,iCol) >= calculateQuantile(upperPart, upperPart.size(), 1.0 - dSpace * (ind+1))){
            y(iRow,iCol) = ind+1;
            break;
          }
        }
      }
    }
  }
  return y;
}
//' Computing the indexes of j-th smallest values of each row
//'
//' This function sorts separately each row of a numeric matrix and returns a matrix
//' in which the value in i-th row and j-th column represent the index of the j-th smallest value of the i-th row.
//'
//' @param x a numeric matrix
//' @return a numeric matrix with indexes indicating positions of j-th smallest element in each row
//'
//' @examples
//' A=matrix(c(4,3,1,2,5,8,6,7),nrow=2,byrow=TRUE)
//' unisort(A)
//'
//' @export
// [[Rcpp::export]]
Rcpp::NumericMatrix unisort(Rcpp::NumericMatrix x) {
  int nr = x.nrow();
  int nc = x.ncol();

  NumericMatrix y(nr,nc);
  int max=omp_get_max_threads();
  omp_set_num_threads(max);


  vector< pair<float,int> > a;
  #pragma omp parallel for private(a)
  for (auto  j=0; j<nr; j++) {
    for (auto  i=0; i<nc; i++) {
      a.push_back(std::make_pair(x(j,i),i));
    }

    stable_sort(a.begin(), a.end());
    for (auto  i=0; i<nc; i++) {
      y(j,i)=a[i].second;
    }
    a.clear();
  }
  return y;
}



//' Calculating a matrix of Longest Common Subsequence (LCS) between a pair of numeric vectors
//'
//' This function calculates the matrix with Longest Common Subsequence (LCS)
//' between two numeric vectors.
//'
//' @param x an integer vector
//' @param y an integer vector
//' @return a matrix storing Longest Common Subsequence (LCS)
//'
//' @examples
//' pairwiseLCS(c(1,2,3,4,5),c(1,2,4))
//'
//' @export
// [[Rcpp::export]]
Rcpp::IntegerMatrix pairwiseLCS(Rcpp::IntegerVector x, Rcpp::IntegerVector y) {

  IntegerMatrix c(x.size()+1,y.size()+1);

  for (auto i=0; i<x.size(); i++) {
    c(i,0)=0;
  }

  for (auto j=0; j<y.size(); j++) {
    c(0,j)=0;
  }

  for(auto i=1; i<x.size()+1; i++) {
    for(auto j=1; j<y.size()+1; j++) {
      if(x(i-1) == y(j-1)) {
        c(i,j) = c(i-1,j-1) + 1;
      }
      else {
        c(i,j) = std::max(c(i,j-1),c(i-1,j));
      }
    }
  }
  return c;
}




//' Retrieving from a matrix Longest Common Subsequence (LCS) between a pair of numeric vector.
//'
//' This function retrieves the Longest Common Subsequence (LCS)
//' between two numeric vectors by backtracking the matrix obtained with dynamic programming.
//'
//' @param x an integer vector
//' @param y an integer vector
//' @return an integer with the length of Longest Common Subsequence (LCS)
//'
//' @examples
//' backtrackLCS( c(1,2,3,4,5),c(1,2,4))
//'
//' @export
// [[Rcpp::export]]
Rcpp::IntegerVector backtrackLCS(Rcpp::IntegerVector x, Rcpp::IntegerVector y) {
  Rcpp::IntegerMatrix c = pairwiseLCS(x,y);
  auto index=c(c.nrow()-1,c.ncol()-1);
  auto i=x.size(), j=y.size();
  Rcpp::IntegerVector lcs(index);

  while (i > 0 && j > 0) {
    if (x(i-1) == y(j-1)) {
      lcs(index-1) = x(i-1);
      i--; j--; index--;
    }
    else if (c(i-1,j) > c(i,j-1))
      i--;
    else
      j--;
   }
   // Print the lcs
   //cout << "LCS of " << X << " and " << Y << " is " << lcs;
  return lcs;
}

//' This function calculates all pairwise LCSes within the array.
//'
//' This function computes unique pairwise Longest Common Subsequences within the matrix.
//'
//' @param discreteInput is a matrix
//' @param useFibHeap boolean value if Fibonacci heap should be used for sorting and seeding
//' @return a list with informa
//'
//' @examples
//' calculateLCS(matrix(c(4,3,1,2,5,8,6,7),nrow=2,byrow=TRUE))
//'
//' @export
// [[Rcpp::export]]
Rcpp::List calculateLCS(Rcpp::IntegerMatrix discreteInput, bool useFibHeap=true) {

  //Copy input data to local vector
  vector<vector<int>> discreteInputData(discreteInput.nrow());
  for (auto it = discreteInputData.begin(); it != discreteInputData.end(); it++) {
    (*it).reserve(discreteInput.ncol());
    for (auto j = 0; j < discreteInput.ncol(); j++) (*it).push_back(discreteInput(it - discreteInputData.begin(), j));
  }
  //calculate the size of output
  int PART = 4;
  int step = discreteInput.nrow()/PART;
  int size = (PART-1)*(step*(step-1)/2);
  int rest = step+(discreteInput.nrow()%PART);
  size+= rest*(rest-1)/2;

  vector<triple> out;
  out.reserve(size);

  internalCalulateLCS(discreteInputData,out, useFibHeap);

  Rcpp::IntegerVector geneA(out.size());
  Rcpp::IntegerVector geneB(out.size());
  Rcpp::IntegerVector lcslen(out.size());

  for(auto i = 0; i < out.size(); i++)
  {
    geneA(i) = out[i].geneA;
    geneB(i) = out[i].geneB;
    lcslen(i) = out[i].lcslen;
  }
  //return triplets;
// TODO: sort according to lcslen. The following sorting doesn't work:
//  std::sort( std::begin(geneA),std::end(geneA), [&](const int &i1, const int &i2) { return lcslen[i1] > lcslen[i2]; } );
//  std::sort( std::begin(geneB),std::end(geneB), [&](const int &i1, const int &i2) { return lcslen[i1] > lcslen[i2]; } );
//  std::sort( std::begin(lcslen),std::end(lcslen), [&](const int &i1, const int &i2) { return lcslen[i1] > lcslen[i2]; } );
//  potential workaround: https://stackoverflow.com/questions/37368787/c-sort-one-vector-based-on-another-one

  return List::create(
           Named("a") = geneA,
           Named("b") = geneB,
           Named("lcslen") = lcslen);


//           Named("order") = triplets);
}




//' Calculating biclusters from sorted list of LCS scores
//'
//'
//' @param discreteInput an integer matrix with indices of sorted columns
//' @param discreteInputValues an integer matrix with values after discretization
//' @param scores a numeric vector
//' @param geneOne a numeric vector
//' @param geneTwo a numeric vector
//' @param rowNumber a int with number of rows
//' @param colNumber a int with number of columns
//' @return a number of found clusters
//'
//' @examples
//' cluster(matrix(c(0,3,1,2,3,1,2,0,1,3,2,0),nrow=4,byrow=TRUE),matrix(c(4,3,1,2,5,8,6,7,9,10,11,12),nrow=4,byrow=TRUE),c(13,12,11,7,5,3),c(0,1,2,0,0,1), c(3,2,3,2,1,3),4,3)
//'
//' @export
// [[Rcpp::export]]
Rcpp::List cluster(Rcpp::IntegerMatrix discreteInput, Rcpp::IntegerMatrix discreteInputValues, Rcpp::IntegerVector scores, Rcpp::IntegerVector geneOne, Rcpp::IntegerVector geneTwo, int rowNumber, int colNumber) {

  //Initialize algorithm parameters
  gParameters.InitOptions(discreteInput.nrow(), discreteInput.ncol());

  //Copy input data to local vector
  vector<vector<int>> discreteInputData(discreteInput.nrow()); 
  for (auto it = discreteInputData.begin(); it != discreteInputData.end(); it++) {
    (*it).reserve(discreteInput.ncol());
    for (auto j = 0; j < discreteInput.ncol(); j++) (*it).push_back(discreteInput(it - discreteInputData.begin(), j));
  }
  int max=omp_get_max_threads();
  omp_set_num_threads(max);
  
  // vector of found bicluster and current bicluster candidate
  vector<BicBlock*> arrBlocks;
  BicBlock *currBlock;

  // helpful vectors/sets
  vector<int> vecBicGenes;
  set<int> vecAllInCluster;

  // matrix of found lcs
  vector<vector<int>> lcsTags(rowNumber);

  //Main loop
  for(auto ind = 0; ind < scores.size(); ind++) {

    /* check if both genes already enumerated in previous blocks */
    bool flag = true;
    /* speed up the program if the rows bigger than 200 */
    if (rowNumber > 250) {
      if ( vecAllInCluster.find(geneOne(ind)) != vecAllInCluster.end() && vecAllInCluster.find(geneTwo(ind)) != vecAllInCluster.end())
        flag = false;
    }
    else {
      flag = check_seed(scores(ind),geneOne(ind), geneTwo(ind), arrBlocks, arrBlocks.size(), rowNumber);
    }
    if (!flag)    {
      continue;
    }
      
    // Init Current block
    currBlock = new BicBlock();
    currBlock->score = min(2, (int)scores(ind));
    currBlock->pvalue = 1;

    // vectors with current genes and scores
    vector<int> vecGenes, vecScores;

    // init the vectors
    vecGenes.reserve(rowNumber);
    vecScores.reserve(rowNumber);
    vecGenes.push_back(geneOne(ind));
    vecGenes.push_back(geneTwo(ind));
    vecScores.push_back(1);
    vecScores.push_back(currBlock->score);

    //set threshold for new candidates for bicluster
    int candThreshold = static_cast<int>(floor(gParameters.ColWidth * gParameters.Tolerance));
    if (candThreshold < 2) 
      candThreshold = 2;

    // vector for candidate rows and their pvalues	
    vector<bool> candidates(rowNumber, true);
    vector<long double> pvalues;

    // init the vectors
    pvalues.reserve(rowNumber);
    candidates[(int)geneOne(ind)] = candidates[(int)geneTwo(ind)] = false;

    // initial components before block init
    int components = 2;

    block_init(scores(ind), geneOne(ind), geneTwo(ind), currBlock, vecGenes, vecScores, candidates, candThreshold, &components, pvalues, &gParameters, lcsTags, &discreteInputData);

    // check new components
    std::size_t  k=0;
    for(k = 0; k < components; k++) {
      if (gParameters.IsPValue)
        if ((pvalues[k] == currBlock->pvalue) &&(k >= 2) &&(vecScores[k]!=vecScores[k+1])) 
          break;
      if ((vecScores[k] == currBlock->score)&&(vecScores[k+1]!= currBlock->score)) 
        break;
    }
 
    components = k + 1;
    if(components > vecGenes.size())
      components = vecGenes.size();
    vecGenes.resize(components);

    // reinitialize candidates vector for further searching
    fill(candidates.begin(), candidates.end(), true);
    for (auto ki=0; ki < vecGenes.size() ; ki++) {
      candidates[vecGenes[ki]] = false;
    }

    // set for column candidates
    set<size_t> colcand;

    // initialize column threshold
    int threshold = floor(components * 0.7)-1;
    if(threshold <1)
      threshold=1;

    //vector for column statistics
    vector<int> colsStat(colNumber,0);


    //calculate column statistics for current components
    vector<vector<int>> temptag(components);
    #pragma omp parallel for default(shared)
    for(auto i=1;i<components;i++) {
      temptag[i] = getGenesFullLCS(discreteInputData[vecGenes[0]], discreteInputData[vecGenes[i]]);
    }
    for(auto i=1;i<components;i++) {
      for(auto jt=temptag[i].begin();jt!=temptag[i].end();jt++){      
          colsStat[*jt]++;
      }
    }
    temptag.clear();
    // insert current column candidates
    for(auto i=0;i<colNumber;i++) {
      if (colsStat[i] >= threshold) {
        colcand.insert(i);
      }
    }

    //--------------------------------------------------------------------------------------------------------------------------------
    // Add new genes

    bool colChose = true;
    vector<int> m_ct(rowNumber);

    // count number of occurances of candidates in results of lcs
    #pragma omp parallel for default(shared)
    for(auto ki=0;ki < rowNumber;ki++) {
      m_ct[ki]= count_if(lcsTags[ki].begin(), lcsTags[ki].end(), [&](int k) { return colcand.find(k) != colcand.end();});
    }

    for(auto ki=0;ki < rowNumber;ki++) {
      colChose=true;
      //check if this candidate can be added
      if (candidates[ki]&& (m_ct[ki] >= floor(colcand.size() * gParameters.Tolerance)-1)) {
        int temp;
        for(auto it=colcand.begin(); it != colcand.end();it++){
          //calculate column statistics of recent candidate
          int tmpcount = colsStat[*it];
          if(find(lcsTags[ki].begin(), lcsTags[ki].end(), *it) != lcsTags[ki].end())
            tmpcount++;
          if(tmpcount < floor(components * 0.1)-1) {
            colChose = false;
            break;
          }
        } 
        if(colChose==true) {
          //add new gene
          vecGenes.push_back(ki);
          components++;
          candidates[ki] = false;
          //update column statistics
          for(auto it=lcsTags[ki].begin();it!=lcsTags[ki].end();it++){      
              colsStat[*it]++;
          }
        }
      }
    }
    currBlock->block_rows_pre = components;

    //------------------------------------------------------------------------------------------------------------------------------------------------
    // Add new genes based on reverse order

     //instersect first lcs input with lcs seed and calculate common vector
     vector<int> g1Common;  
     for (auto i = 0; i < discreteInputData[vecGenes[0]].size() ;i++){
       auto res = find(lcsTags[vecGenes[1]].begin(), lcsTags[vecGenes[1]].end(), discreteInputData[vecGenes[0]][i]);
       if(res!=lcsTags[vecGenes[1]].end())
         g1Common.push_back(discreteInputData[vecGenes[0]][i]);
     }

     vector<int> g2Common;
     vector<vector<int>> reveTag(rowNumber);
     #pragma omp parallel for default(shared) private(g2Common)
     for (auto ki = 0; ki < rowNumber; ki++) {
        //instersect second lcs input with lcs seed and calculate common vector
      for (auto i = 0; i < discreteInputData[ki].size() ;i++){
        auto res = find(lcsTags[vecGenes[1]].begin(), lcsTags[vecGenes[1]].end(), discreteInputData[ki][i]);
        if(res!=lcsTags[vecGenes[1]].end())
          g2Common.push_back(discreteInputData[ki][i]);
      }
      //reverse the second input
      reverse(g2Common.begin(), g2Common.end());
      //calculate the lcs
      reveTag[ki] = getGenesFullLCS(g1Common, g2Common);
      g2Common.clear();
      // count number of occurances of candidates in results of lcs
      m_ct[ki]= count_if(reveTag[ki].begin(), reveTag[ki].end(), [&](int k) { return colcand.find(k) != colcand.end();});
     }
    

    for (auto ki = 0; ki < rowNumber; ki++) {
      colChose=true;
      //vector for result from lcs with reversed input
      int commonCnt=0;
      for (auto i=0;i<colNumber;i++) {
        if (discreteInputValues(vecGenes[0],i) * (discreteInputValues(ki,i)) != 0)
          commonCnt++;
      }
      if(commonCnt< floor(colcand.size() * gParameters.Tolerance)) {
        candidates[ki] = false;
        continue;
      }
      //check if this candidate can be added
      if (candidates[ki] && (m_ct[ki] >= floor(colcand.size() * gParameters.Tolerance)-1)) {
        for(auto it=colcand.begin(); it != colcand.end();it++){
          //calcualte columns statistics of candidate
          int tmpcount = colsStat[*it];
          if(find(reveTag[ki].begin(), reveTag[ki].end(), *it) != reveTag[ki].end())
            tmpcount++;
          if(tmpcount < floor(components * 0.1)-1) {
            colChose = false;
            break;
          }
        }
        if(colChose==true) {
          //add new gene
          vecGenes.push_back(ki);
          components++;
          candidates[ki] = false;
          //update column statistics
          for(auto it=reveTag[ki].begin();it!=reveTag[ki].end();it++)
          {      
              colsStat[*it]++;
          }
        }
      }
    }
    // save the current cluster
    for (auto ki = 0; ki < currBlock->block_rows_pre; ki++)
      vecBicGenes.push_back(vecGenes[ki]);

    // add conditions to current bicluster
    for (auto it = colcand.begin(); it!=colcand.end(); it++)
        currBlock->conds.push_back(*it);
    currBlock->block_cols = currBlock->conds.size();

    // check the minimal requirements for bicluster
    if (currBlock->block_cols < 4 || components < 5){
      delete currBlock;
      continue;      
    }
    currBlock->block_rows = components;

    // update score of current bicluster
    if (gParameters.IsPValue)
      currBlock->score = -(100*log(currBlock->pvalue));
    else
      currBlock->score = currBlock->block_rows * currBlock->block_cols;

    // add genes to current bicluster
    currBlock->genes.clear();    
    for (auto ki=0; ki < components; ki++){
      currBlock->genes.push_back(vecGenes[ki]);
      // update vector with all found genes
      auto result1 = find(vecAllInCluster.begin(), vecAllInCluster.end(), vecGenes[ki]);
      if(result1==vecAllInCluster.end())
        vecAllInCluster.insert(vecGenes[ki]);
    }
    // add current block to vector
    arrBlocks.push_back(currBlock);

    // check termination condition 
    if (arrBlocks.size() == gParameters.SchBlock) 
      break;
  }

  //------------------------------------------------------------------------------------------------------------------------------------
  // Sorting and postprocessing of biclusters

  sort(arrBlocks.begin(), arrBlocks.end(), &blockComp);
  int n = min(static_cast<int>(arrBlocks.size()), gParameters.RptBlock);
  bool flag;

  BicBlock **output = new BicBlock*[n]; // Array with filtered biclusters
  BicBlock **bb_ptr = output;
  BicBlock *b_ptr;

  double cur_rows, cur_cols;
  double inter_rows, inter_cols;

  /* the major post-processing here, filter overlapping blocks*/
  int i = 0, j = 0, k=0;
  while (i < arrBlocks.size() && j < n) {
    b_ptr = arrBlocks[i];
    cur_rows = b_ptr->block_rows;
    cur_cols = b_ptr->block_cols;
    
    flag = true;
    k = 0;
    while (k < j) {
      inter_rows =0;
      for(auto iter = output[k]->genes.begin(); iter != output[k]->genes.end(); iter++) {
        auto result1 = find(b_ptr->genes.begin(), b_ptr->genes.end(), *iter);
        if(result1!=b_ptr->genes.end()){
          inter_rows++;
        }
      }
      inter_cols=0;
      for(auto iter = output[k]->conds.begin(); iter != output[k]->conds.end(); iter++) {
        auto result1 = find(b_ptr->conds.begin(), b_ptr->conds.end(), *iter);
        if(result1!=b_ptr->conds.end()){
          inter_cols++;
        }
      }
      if (inter_rows*inter_cols > gParameters.Filter*cur_rows*cur_cols) {
        flag = FALSE;
        break;
      }
      k++;
    }
    i++;
    if (flag) {
      // print_bc(fw, b_ptr, j++); file print
      j++;
      *bb_ptr++ = b_ptr;
    }
  }
  List outList = fromBlocks(output, j, rowNumber, colNumber);
  for(auto ind =0; ind<arrBlocks.size(); ind++)
      delete arrBlocks[ind];
  delete[] output;

  return outList;
}
Rcpp::List fromBlocks(BicBlock ** blocks, const int numBlocks, const int nr, const int nc) {

  auto x = LogicalMatrix(nr, numBlocks);
  auto y = LogicalMatrix(numBlocks, nc);
  for (int i = 0; i < numBlocks; i++) {
    for (auto it = blocks[i]->genes.begin(); it != blocks[i]->genes.end(); ++it) 
      x(*it, i) = true;
    for (auto it = blocks[i]->conds.begin(); it != blocks[i]->conds.end(); ++it)   
      y(i, *it) = true;
  }
  return List::create(
           Named("RowxNumber") = x,
           Named("NumberxCol") = y,
           Named("Number") = numBlocks,
           Named("info") = List::create());
}



/*
// Unoptimized version of unisort
// [[Rcpp::export]]

Rcpp::NumericMatrix unisort_not_optimal(Rcpp::NumericMatrix x){
  NumericMatrix y(x);
  int nr = x.nrow();
  int nc = x.ncol();

  vector< pair<float,int> > a;
  for (int j=0; j<nr; j++) {
    for (int i=0; i<nc; i++) {
      a.push_back(std::make_pair(x(j,i),i));
    }

    sort(a.begin(), a.end());
    for (int i=0; i<nc; i++) {
      y(j,i)=a[i].second;
    }
    a.clear();
  }
  return y;
}
*/

