

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
//#include <sys/socket.h>
#include <stdlib.h>
//#include <netinet/in.h>
//#include <arpa/inet.h>
#include <sys/time.h>
#include <unistd.h>
//#include <bits/stdc++.h> 


#include <seqan/basic.h>
#include <seqan/sequence.h>
#include <seqan/seq_io.h>
#include <seqan/alignment_free.h>


#include <iostream>
#include <sstream>
#include <string>
#include <fstream>

#define OK (0)
#define ERROR (-1)
#define SIZEERROR (1)
#define BPERROR (2)
#define MINSIZE (3000000)
//#define MINSIZE (10)
#define MINBP (0.90)
#define MIND2 (6.0)
#define KN (11)
#define KD (14)
#define MINTIME (1200000)

using namespace std;

using namespace seqan;

int alf() {
    std::ostringstream ss;
    typedef Matrix<double, 2> TMatrix;
    TMatrix myMatrix;  // myMatrix stores pairwise kmerScores
    typedef String<Dna5>        TText;
    typedef StringSet<TText>    TStringSet;
    TStringSet mySequenceSet;  // mySequenceSet stores all sequences from the multi-fasta file

    SeqFileIn seqFile;
    open(seqFile, toCString("alf.fasta"));
    StringSet<CharString> seqIDs;
    readRecords(seqIDs, mySequenceSet, seqFile);
    AFScore<N2> myScoreN2(KN, 1, "", 0, 0.1, "", 0);
    //AFScore<D2> myScoreD2(KD, "");
    alignmentFreeComparison(myMatrix, mySequenceSet, myScoreN2);
    //alignmentFreeComparison(myMatrix, mySequenceSet, myScoreD2);
    stringstream temp3;
    string temp2;
    temp3 << myMatrix;
    temp2 = "";
    temp3 >> temp2;
    temp2 = "";
    temp3 >> temp2;
    // cout << "\n" << myMatrix << "\n";
    // cout << "\n Alf-N2: " << temp2 << "\n";
    double r;
    r = strtof(temp2.c_str(),0);
    //r = log(r);
    //float bp = 0.0;
    
    //alf-n2 polinomial regression
    //bp = 0.0103 - 1.1448*r + 15.957*pow(r,2) - 34.504*pow(r,3) +  20.168*pow(r,4);
    //bp = -0.039 + 1.665*r - 4.572*pow(r,2) + 2.321*pow(r,3) +  1.095*pow(r,4);  
    //printf ("\n BP: %f \n", bp);
    if (r < MINBP)
      return(BPERROR);
    else
      return(OK);
}

int merge_files(char s1[50], char s2[50], char s3[50]) {

// Open two files to be merged
   FILE *fp1 = fopen(s1, "r");
   FILE *fp2 = fopen(s2, "r");
   std::ostringstream ss;
   string command; 

   // Open file to store the result
   FILE *fp3 = fopen(s3, "wt");
/*    char c;

   if (fp1 == NULL || fp2 == NULL || fp3 == NULL)    {
         puts("Error: could not open files");
         return(ERROR);
   }

   // Copy contents of first file to s3.txt
   while ((c = fgetc(fp1)) != EOF)
      fputc(c, fp3);

   // Copy contents of second file to s3.txt
   while ((c = fgetc(fp2)) != EOF)
      fputc(c, fp3);

*/ 
   
   command = "cat ";
   ss.clear();
   ss.str("");
   ss << s1;
   command = command + ss.str();
   ss.clear();
   ss.str("");
   ss << s2;
   //cout << ss.str();
   command = command + " " + ss.str();
   ss.clear();
   ss.str("");
   ss << s3;
   command = command + " > " + ss.str();
   //cout << "\n\n " << command;
   system (command.c_str());

   //printf("Merged file1.txt and file2.txt into s3.txt");

   fclose(fp1);
   fclose(fp2);
   fclose(fp3);
   return (OK);

} 



int check_size  (char s1[50], char s2[50], int gflops) {

    FILE * fp1;
    long int sizefiles = 1;
    double estimate = 0.0;

    fp1 = fopen(s1, "r");
    if(fp1 == NULL)  
        return(ERROR);
    fseek(fp1, 0, SEEK_END);
    if (ftell(fp1) < MINSIZE) {    
       fclose(fp1);
       return(SIZEERROR);
    }
    else {
      sizefiles = sizefiles * ftell(fp1);
      fclose (fp1);
    }
    fp1 = fopen(s2, "r");
    if(fp1 == NULL)
        return(ERROR);
    fseek(fp1, 0, SEEK_END);
    if (ftell(fp1) < MINSIZE) {
        fclose(fp1);
        return(SIZEERROR);
    }
    else {
      sizefiles = sizefiles * ftell(fp1);
      fclose (fp1);
    }
    estimate = sizefiles/(gflops*10000);
    // cout << "\n size: " << sizefiles << "   estimate: " << estimate << "\n";
    if (estimate < MINTIME)
        return(ERROR);
    return(OK);

}


int main(int argc, char *argv[]) {

  char str1[50], str2[50], strfasta[50];
  int ret;
  int gflops = 0;
  char fileout[50] = "decision.txt";
  FILE * fp;

  if (argc != 4) {
    printf ("\n Usage error: please provide two fasta files and the GFlops odf the GPUs. \n");
    return(ERROR);
  }

  strcpy(strfasta,"alf.fasta");
  //strcpy(tp,argv[1]);
  strcpy(str1,argv[1]);
  strcpy(str2,argv[2]);
  gflops = atoi(argv[3]);
   

  ret = merge_files(str1, str2, strfasta);
  if (ret < 0) {
    fp = fopen(fileout,"wt");
    fprintf (fp, "%d", ERROR);
    fclose(fp); 
    return(ERROR);
  }

  ret = check_size(str1, str2, gflops);
  if (ret == OK) {
     fp = fopen(fileout,"wt");
     fprintf (fp, "%c", '-');
     fclose(fp);
  }
  else {
     fp = fopen(fileout,"wt"); 
     fprintf (fp, "%d", SIZEERROR);
     fclose(fp);
     return(SIZEERROR);
  }
  ret = alf();
  if (ret != OK) {
     fp = fopen(fileout,"wt");
     fprintf (fp, "%d", BPERROR);
     fclose(fp);
     return(BPERROR);
  }
  
  fp = fopen(fileout,"wt");
  fprintf (fp, "%d", OK);
  fclose(fp);
  return(OK);

}
