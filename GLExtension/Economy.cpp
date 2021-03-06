//
//  Economy.cpp
//  GLExtension
//
//  Created by David Evans on 5/16/11.
//  Copyright 2011 __MyCompanyName__. All rights reserved.
//

#include "Economy.h"
#include <boost/random/uniform_real.hpp>
#include <boost/random/variate_generator.hpp>
#include <boost/random/normal_distribution.hpp>
#include <omp.h>

using namespace arma;

economy::economy(double param[],const arma::vec &kap, const arma::mat &pi_k, const arma::vec &reg):F(param,kap,pi_k,reg),kappa(kap),Pi_k(pi_k), c_reg(reg)
{
    //Set up parameters
    psi = param[0];
    beta = param[1];
    var_e = param[2];
    var_u = param[3];
    sigma_e = sqrt(var_e);
    sigma_u = sqrt(var_u);
    rho = param[4];
    gamma = param[5];
    n_k = kappa.n_rows;
    cumPi_k = join_rows(zeros<vec>(n_k),cumsum(Pi_k,1));
    
    //compute stationary distribution
    mat l_eigvec, r_eigvec;
    cx_vec eigval;
    eig_gen(eigval, l_eigvec, r_eigvec, Pi_k);
    uvec index = sort_index(abs(real(eigval)-1));
    Pi_kstat = l_eigvec.col(index(0));
    Pi_kstat /= sum(Pi_kstat);
    
    
    F.solveBellman();
}

/*
 *Simulate the economy Tmax number of periods for Nfirms firms.
 */
vec economy::simulateSeries(int Tburn, int Tmax, int Nfirms, int seed)
{
    cout<<"Simulating: "<<endl;
    gen.seed(seed);
    boost::normal_distribution<> ndist(0.0,1.0);
    boost::variate_generator<boost::mt19937&, boost::normal_distribution<> > ngen(gen, ndist);
    //Initialize;
    vec mu;
    if(mu.load("mu.mat"))
    {
        if(mu.n_rows != Nfirms ){
            mu = gamma/(gamma-1)*ones<vec>(Nfirms,1);
        }
    }else{
        mu = gamma/(gamma-1)*ones<vec>(Nfirms,1);
    }
    
    //Set up initial fixed cost distribution.
    vec cumStatDist = join_cols(zeros<vec>(1), cumsum(Pi_kstat)); 
    ivec kap = zeros<ivec>(Nfirms);
    for(int j =0; j<Nfirms; j++)
    {
        kap(j) = drawDiscrete(cumStatDist);
    }
    vec Fchange = zeros<vec>(Tmax+1);
    vec M(Tmax+1);
    M(0) = 1;
    vec g(Tmax+1);
    g(0) = 0;
    vec p(Tmax+1);//really log p
    p(0) = as_scalar(mean(pow(mu, 1-gamma)));
    p(0) = log(p(0))/(1-gamma);
    vec pchange = zeros<vec>(Tmax+1);
    vec pchangestd = zeros<vec>(Tmax+1);
    vec pchangestate;
    //Run Economy
    for (int t =1; t<Tmax+1; t++) {
        g(t) = rho*g(t-1)+ngen()*sigma_u;
        M(t) = M(t-1)+exp(g(t));
        pchangestate = zeros<vec>(Nfirms);
        double P;
        if (t%50 ==0)
            cout<< t<<endl;
        //remember private x when parallelizing.
#pragma omp parallel
        {
            vec x;
            x << 0.0 <<g(t) <<p(t-1)<<endr;
#pragma omp for
            for (int j =0; j<Nfirms; j++) {
                double ea;
#pragma omp critical
                ea = ngen()*sigma_e;
                
                //update mu
                x(0) = mu(j)/exp(g(t)+ea);
                //get new mu
                mu(j) = F.getPolicy(x, kap(j));
                if (mu(j) != x(0)) {
#pragma omp critical
                    {
                        Fchange(t)++;
                        //pchange(t) += std::abs( std::log(mu(j)/x(0)) );
                    }
                    pchangestate(j) =std::log(mu(j)/x(0));
                }
                //add to price
#pragma omp critical
                P += pow(mu(j),1-gamma);
                //get new kappa
                kap(j) = drawKappa(kap(j));
            }
        }
        pchange(t) = (mean(vec(abs(pchangestate)))*Nfirms)/Fchange(t);
        pchangestd(t) = stddev(pchangestate,1)*sqrt(Nfirms)/sqrt(Fchange(t));
        Fchange(t) /=Nfirms;
        P /= Nfirms;
        //get log P
        p(t) = log(P)/(1-gamma);
        F.checkPbound(p(t));
        
    }
    
    int n = Tmax - Tburn-1;
    //Perform Regression
    vec y = p.rows(Tburn+2, Tmax);
    mat X = join_rows(g.rows(Tburn+2,Tmax), p.rows(Tburn+1,Tmax-1));
    X = join_rows(ones<vec>(n), X);
    cout<<"Average number of changes: "<<mean(Fchange.rows(Tburn+1, Tmax))<<endl;
    cout<<"Average size of price change: "<<mean(pchange.rows(Tburn+1,Tmax))<<endl;
    cout<<"Average std of price changes:  "<<mean(pchangestd.rows(Tburn+1,Tmax))<<endl;
    mu.save("mu.mat");
    return solve(trans(X)*X,trans(X)*y);
}

/*
 *Simulate the economy Tmax number of periods for Nfirms firms.
 */
void economy::storeOutcome(int Tburn, int Tmax, int Nfirms, int seed, double shock,int ver)
{
    gen.seed(seed);
    boost::normal_distribution<> ndist(0.0,1.0);
    boost::variate_generator<boost::mt19937&, boost::normal_distribution<> > ngen(gen, ndist);
    //Initialize;
    vec mu;
    if(mu.load("mu.mat"))
    {
        if(mu.n_rows != Nfirms ){
            mu = gamma/(gamma-1)*ones<vec>(Nfirms,1);
        }
    }else{
        mu = gamma/(gamma-1)*ones<vec>(Nfirms,1);
    }
    mat muhist(Nfirms,Tmax+1);
    mat ahist(Nfirms,Tmax+1);
    ahist.col(0) = ones<vec>(Nfirms);
    
    //Set up initial fixed cost distribution.
    vec cumStatDist = join_cols(zeros<vec>(1), cumsum(Pi_kstat)); 
    ivec kap = zeros<ivec>(Nfirms);
    for(int j =0; j<Nfirms; j++)
    {
        kap(j) = drawDiscrete(cumStatDist);
    }
    vec Fchange = zeros<vec>(Tmax+1);
    vec M(Tmax+1);
    M(0) = 1;
    vec g(Tmax+1);
    g(0) = 0;
    vec p(Tmax+1);//really log p
    p(0) = 0;
    vec ea(Nfirms);
    //Run Economy
    for (int t =1; t<Tmax+1; t++) {
        g(t) = rho*g(t-1)+ngen()*sigma_u;
        M(t) = M(t-1)*exp(g(t));
        double P;
        //Apply monetary shock for impulse response
        if (t == Tburn) {
            mu /= 1+shock;
            M(t) *=1+shock;
        }
        for (int j =0; j<Nfirms; j++) 
        {
            ea(j) = ngen()*sigma_e;
        }
        if( t%50 == 0)
            cout<<t<<endl;
        //remember private x when parallelizing.
#pragma omp parallel
        {
            vec x;
            x << 0.0 <<g(t) <<p(t-1)<<endr;
#pragma omp for
            for (int j =0; j<Nfirms; j++) {
                
                ahist(j,t) = ahist(j,t-1)*exp(ea(j));
                //update mu
                x(0) = mu(j)/exp(g(t)+ea(j));
                //get new mu
                mu(j) = F.getPolicy(x, kap(j));
                if (mu(j) != x(0)) {
#pragma omp critical
                    Fchange(t)++;
                }
                //add to price
#pragma omp critical
                P += pow(mu(j),1-gamma);
                //get new kappa
                
            }
        }
        for(int j=0;j<Nfirms;j++)
        {
            kap(j) = drawKappa(kap(j));
        }
        muhist.col(t) = mu;
        Fchange(t) /=Nfirms;
        P /= Nfirms;
        //get log P
        p(t) = log(P)/(1-gamma);
        
    }
    
    int n = Tmax - Tburn-1;
    //Perform Regression
    vec y = p.rows(Tburn+2, Tmax);
    mat X = join_rows(g.rows(Tburn+2,Tmax), p.rows(Tburn+1,Tmax-1));
    X = join_rows(ones<vec>(n), X);
    cout<<"Average number of changes: "<<accu(Fchange)/(Tmax+1)<<endl;
    vec belief =  solve(trans(X)*X,trans(X)*y);
    
    //Save results
    std::stringstream name;
    name << "/scratch/dge218/muhist" << ver <<".mat";
    mat(muhist.rows(1,1000)).save(name.str(),raw_ascii);
    name.str("");
    
    name << "/scratch/dge218/ahist" << ver <<".mat";
    mat(ahist.rows(1,1000)).save(name.str(),raw_ascii);
    name.str("");
    
    name << "/scratch/dge218/p" << ver <<".mat";
    p.save(name.str(),raw_ascii);
    name.str("");
    
    name << "/scratch/dge218/g" << ver <<".mat";
    g.save(name.str(),raw_ascii);
    name.str("");
    
    name << "/scratch/dge218/M" << ver <<".mat";
    M.save(name.str(),raw_ascii);
    name.str("");
    
    name << "/scratch/dge218/pchange" << ver <<".mat";
    Fchange.save(name.str(),raw_ascii);
    name.str("");
}

/*
 *Draws from the cumulative distribution cumDist.
 */
int economy::drawDiscrete(const vec& cumDist)
{
    //Assuming cumDist starts with 0 and ends with 1, draw a number between 0 and n-1 then cumDist has n+1 rows
    boost::uniform_real<> dist(0,1.0);
    boost::variate_generator<boost::mt19937&, boost::uniform_real<> > udist(gen, dist);
    double rand;
#pragma omp critical
    rand = udist();
    int j;
    for (j = cumDist.n_rows-2; j>=0; j--) {
        if (rand >= cumDist(j)) 
            break;
    }
    return j;
}







