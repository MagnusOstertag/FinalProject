#include "computation.h"

void Computation::initialize(int argc, char *argv[])
{
    // parse the parameters
    settings_.loadFromFile(argv[1]);
    settings_.printSettings();

    // calculate
    for (int i = 0; i < 2; i++)
    {
        meshWidth_[i] =  settings_.physicalSize[i] / settings_.nCells[i];
        std::cout << "computed mesh width " << i << ": " << meshWidth_[i] << " " << settings_.physicalSize[i] << " " << settings_.nCells[i] << std::endl;
    }

    // initialize
    if (settings_.useDonorCell)
    {
        discretization_ = std::make_shared<DonorCell>(settings_.nCells, meshWidth_, settings_.alpha);
    } else {
        discretization_ = std::make_shared<CentralDifferences>(settings_.nCells, meshWidth_);
    }
    
    if (settings_.pressureSolver == "SOR")
    {
        pressureSolver_ = std::make_unique<SOR>(discretization_, settings_.epsilon, settings_.maximumNumberOfIterations, settings_.omega);
    } else if (settings_.pressureSolver == "GaussSeidel")
    {
        pressureSolver_ = std::make_unique<GaussSeidel>(discretization_, settings_.epsilon, settings_.maximumNumberOfIterations);
    } else 
    {
        std::cout << "The name of the pressure solver is not understood" << settings_.pressureSolver << std::endl;
        throw;
    }   

    // misc
    dt_ = 0.;

    // initialize output writer
    outputWriterParaview_ = std::make_unique<OutputWriterParaview>(discretization_);
    outputWriterText_     = std::make_unique<OutputWriterText>(discretization_);
}

void Computation::runSimulation()
{
    double currentTime = 0;

    std::cout << "+++++++++++++++++++++++" << std::endl;
    std::cout << "Starting at time: " << currentTime << std::endl;
    std::cout << "+++++++++++++++++++++++" << std::endl;

    // the steps correspond to the steps in our algorithm in the overleaf or docs/numsim-algos.tex
    while (currentTime < settings_.endTime)
    {
        // step 1: set the boundary values
        applyBoundaryValues();
        std::cout << "Applied boundary values for u/v and F/G." << std::endl;

        // step 2: compute time step width
        computeTimeStepWidth();

        std::cout << "Computed time step width: " << dt_ << std::endl;

        // set the dt such that the simulation stops exactly on endTime
        if (currentTime + dt_ > settings_.endTime)
        {
            dt_ = settings_.endTime - currentTime;

            std::cout << std::endl;
            std::cout << "Final time step!" << std::endl;
        }
        currentTime += dt_;

        std::cout << "+++++++++++++++++++++++" << std::endl;
        std::cout << "current Time: " << currentTime << std::endl;
        std::cout << "+++++++++++++++++++++++" << std::endl;

        // step 4: calculate F, G with first setting the boundary conditions of F, G (step 3)
        computePreliminaryVelocities();

        std::cout << "Computed preliminary velocities" << std::endl;

        // step 5: compute the right hand side of the pressure equation
        computeRightHandSide();

        std::cout << "Computed right hand side" << std::endl;

        // step 6: solve the pressure equation
        computePressure();

        std::cout << "Computed presure" << std::endl;

        // step 7: calculate the final velocities
        computeVelocities();

        std::cout << "Computed velocities" << std::endl;

        // step 9: write output
        outputWriterParaview_->writeFile(currentTime);
        outputWriterText_->writeFile(currentTime);
    }
}

void Computation::computeTimeStepWidth()
{
    double boundary_diffusion = 0.;

    // boundary from diffusion
    if (meshWidth_[0] == meshWidth_[1])
    {
        boundary_diffusion = (settings_.re * meshWidth_[0] * meshWidth_[1]) / 4.;
    } else
    {
        double h2x = meshWidth_[0] * meshWidth_[0];
        double h2y = meshWidth_[1] * meshWidth_[1];
        boundary_diffusion = (settings_.re / 2.) * (h2x * h2y) * (1./(h2x + h2y));
      
        std::cout << settings_.re << " " << h2x << " " << h2y << std::endl;
    }

    // calculate max absolute velocities
    double u_max = 0.;
    for ( int j = discretization_->uJBegin(); j < discretization_->uJEnd(); j++)
    { 
        for ( int i = discretization_->uIBegin(); i < discretization_->uIEnd(); i++)
        {
            double value = std::abs(discretization_->u(i,j));
            if (value > u_max)
            {
                u_max = value;
            }
        }
    }

    double v_max = 0.;
    for ( int j = discretization_->vJBegin(); j < discretization_->vJEnd(); j++)
    { 
        for ( int i = discretization_->vIBegin(); i < discretization_->vIEnd(); i++)
        {
            double value = std::abs(discretization_->v(i,j));
            if (value > v_max)
            {
                v_max = value;
            }
        }
    }

    // boundary from convection
    double boundary_convection_u = meshWidth_[0] / u_max;
    double boundary_convection_v = meshWidth_[1] / v_max;

    // together
    double min_dt = std::min({boundary_diffusion, boundary_convection_u, boundary_convection_v});

    std::cout << "dt boundaries - diffusion: " << boundary_diffusion << " convection_u: " << boundary_convection_u << " convection_v: " << boundary_convection_v << std::endl;

    // security factor
    dt_ =  std::min(min_dt * settings_.tau, settings_.maximumDt);
}
    
void Computation::applyBoundaryValues()
{
    // bottom and top
    // set u
    for ( int i = discretization_->uIBegin(); i < discretization_->uIEnd(); i++)
    {
        // bottom  
        discretization_->u(i, discretization_->uJBegin())   = 2. * settings_.dirichletBcBottom[0] - discretization_->u(i, discretization_->uJBegin() +1);
        
        // top
        discretization_->u(i, discretization_->uJEnd() - 1) = 2. * settings_.dirichletBcTop[0] - discretization_->u(i, discretization_->uJEnd() -2);
    }
    
    // set v
    for ( int i = discretization_->vIBegin(); i < discretization_->vIEnd(); i++)
    {
        // bottom
        discretization_->v(i, discretization_->vJBegin())  = settings_.dirichletBcBottom[1];
        
        // top
        discretization_->v(i, discretization_->vJEnd() - 1) = settings_.dirichletBcTop[1];
    }

    // sides - they take priority then (corners are overwritten)
    // set u
    for ( int j = discretization_->uJBegin(); j < discretization_->uJEnd(); j++)
    {
        // left
        discretization_->u(discretization_->uIBegin(),j)   = settings_.dirichletBcLeft[0];

        // right
        discretization_->u(discretization_->uIEnd() -1 ,j) = settings_.dirichletBcRight[0];
    }
    // set v
    for ( int j = discretization_->vJBegin(); j < discretization_->vJEnd(); j++)
    {
        // left
        discretization_->v(discretization_->vIBegin() ,j)  = 2. * settings_.dirichletBcLeft[1] - discretization_->v(discretization_->vIBegin() + 1, j);

        // right
        discretization_->v(discretization_->vIEnd() - 1,j) = 2. * settings_.dirichletBcRight[1] - discretization_->v(discretization_->vIEnd()  - 2, j);
    }

    // set f,g boundary conditions the same as u,v
    // bottom and top
    // set f
    for ( int i = discretization_->uIBegin(); i < discretization_->uIEnd(); i++)
    {
        // bottom
        discretization_->f(i, discretization_->uJBegin()) = discretization_->u(i, discretization_->uJBegin());
        
        // top
        discretization_->f(i, discretization_->uJEnd() -1) = discretization_->u(i, discretization_->uJEnd() -1);
    }
    // set g
    for ( int i = discretization_->vIBegin(); i < discretization_->vIEnd(); i++)
    {
        // bottom
        discretization_->g(i, discretization_->vJBegin()) = discretization_->v(i, discretization_->vJBegin());
        
        // top
        discretization_->g(i, discretization_->vJEnd() -1) = discretization_->v(i, discretization_->vJEnd() -1);
    }

    // sides
    // calculating first the top/bottom then the sides gives the sides priority as required by the hint
    // set f
    for ( int j = discretization_->uJBegin(); j < discretization_->uJEnd(); j++)
    {
        // left
        discretization_->f(discretization_->uIBegin(),j) = discretization_->u(discretization_->uIBegin(),j);

        // right
        discretization_->f(discretization_->uIEnd() -1,j) = discretization_->u(discretization_->uIEnd() -1,j);
    }
    // set g
    for ( int j = discretization_->vJBegin(); j < discretization_->vJEnd(); j++)
    {
        // left
        discretization_->g(discretization_->vIBegin(),j) = discretization_->v(discretization_->vIBegin(),j);

        // right
        discretization_->g(discretization_->vIEnd() -1,j) = discretization_->v(discretization_->vIEnd() -1,j);
    }
}
    
void Computation::computePreliminaryVelocities()
{
    // calculate F
    for ( int j = discretization_->uJBegin() +1; j < discretization_->uJEnd() -1; j++)
    { 
        for ( int i = discretization_->uIBegin() +1; i < discretization_->uIEnd() -1; i++)
        {
            double diffusion = discretization_->computeD2uDx2(i,j) + discretization_->computeD2uDy2(i,j);
            double convection = - discretization_->computeDu2Dx(i,j) - discretization_->computeDuvDy(i,j);
            double sum =  ((1. / settings_.re) * diffusion) + convection + settings_.g[0];
            discretization_->f(i,j) = discretization_->u(i, j) + dt_ *  sum;
        }
    }

    // calculate G
    for ( int j = discretization_->vJBegin() +1; j < discretization_->vJEnd() -1; j++)
    { 
        for ( int i = discretization_->vIBegin() +1; i < discretization_->vIEnd() -1; i++)
        {
            double diffusion = discretization_->computeD2vDx2(i,j) + discretization_->computeD2vDy2(i,j);
            double convection = - discretization_->computeDuvDx(i,j) - discretization_->computeDv2Dy(i,j);
            double sum = ((1. / settings_.re) * diffusion) + convection + settings_.g[1];
            discretization_->g(i,j) = discretization_->v(i, j) + dt_ * sum;
        }
    }
}
    
void Computation::computeRightHandSide()
{
    for ( int j = discretization_->pJBegin() +1; j < discretization_->pJEnd() -1; j++)
    { 
        for ( int i = discretization_->pIBegin() +1; i < discretization_->pIEnd() -1; i++)
        {
            double difference_f = (discretization_->f(i,j) - discretization_->f(i-1,j)) / discretization_->dx();
            double difference_g = (discretization_->g(i,j) - discretization_->g(i,j-1)) / discretization_->dy();
            discretization_->rhs(i,j) = (1 / dt_) * (difference_f + difference_g);
        }
    }
}
    
void Computation::computePressure()
{
    pressureSolver_->solve();
}

void Computation::computeVelocities()
{
    // calculate u
    for ( int j = discretization_->uJBegin() +1; j < discretization_->uJEnd() -1; j++)
    { 
        for ( int i = discretization_->uIBegin() +1; i < discretization_->uIEnd() -1; i++)
        {
            discretization_->u(i,j) = discretization_->f(i,j) - dt_ * discretization_->computeDpDx(i, j);
        }
    }

    // calculate v
    for ( int j = discretization_->vJBegin() +1; j < discretization_->vJEnd() -1; j++)
    { 
        for ( int i = discretization_->vIBegin() +1; i < discretization_->vIEnd() -1; i++)
        {
            discretization_->v(i,j) = discretization_->g(i,j) - dt_ * discretization_->computeDpDy(i, j);
        }
    }
}

void Computation::runTest()
{
    std::cout << settings_.pressureSolver << " " << settings_.useDonorCell <<std::endl;
    //test pressure solver
    //test case 1: rhs = 0, p = const.
    std::cout << "+++++++++++++++++++++++++++++++++++ TASK 1" << std::endl;
    int fieldWidth = 9;
     for (int j = discretization_->pJEnd()-1; j >= discretization_->pJBegin(); j--)
     {
       std::cout << std::setw(fieldWidth) << j << "|";
       for (int i = discretization_->pIBegin(); i < discretization_->pIEnd(); i++)
       {
           std::cout<< std::setw(fieldWidth) << std::setprecision(fieldWidth-6) << discretization_->p(i,j);
       }
       std::cout << std::endl;
     }
     std::cout << std::endl;

     for ( int j = discretization_->pJBegin() +1; j < discretization_->pJEnd() -1; j++)
    { 
        for ( int i = discretization_->pIBegin() +1; i < discretization_->pIEnd() -1; i++)
        {
            discretization_->rhs(i,j) = 0;
            discretization_->p(i,j) = 1;


        }
    }    
    std::cout << "+++++++++++++++++++++++++++++++++++" << std::endl;

    pressureSolver_->solve();
   for (int j = discretization_->pJEnd()-1; j >= discretization_->pJBegin(); j--)
     {
       std::cout << std::setw(fieldWidth) << j << "|";
       for (int i = discretization_->pIBegin(); i < discretization_->pIEnd(); i++)
       {
           std::cout<< std::setw(fieldWidth) << std::setprecision(fieldWidth-6) << discretization_->p(i,j);
       }
       std::cout << std::endl;
     }
     std::cout << std::endl;

     std::cout << settings_.pressureSolver << " " << settings_.useDonorCell <<std::endl;


    //test pressure solver
    //test case 2: rhs = const., p = 0
    std::cout << "+++++++++++++++++++++++++++++++++++ TASK 2" << std::endl;
     for (int j = discretization_->pJEnd()-1; j >= discretization_->pJBegin(); j--)
     {
       std::cout << std::setw(fieldWidth) << j << "|";
       for (int i = discretization_->pIBegin(); i < discretization_->pIEnd(); i++)
       {
           std::cout<< std::setw(fieldWidth) << std::setprecision(fieldWidth-6) << discretization_->p(i,j);
       }
       std::cout << std::endl;
     }
     std::cout << std::endl;

     for ( int j = discretization_->pJBegin() +1; j < discretization_->pJEnd() -1; j++)
    { 
        for ( int i = discretization_->pIBegin() +1; i < discretization_->pIEnd() -1; i++)
        {
            discretization_->rhs(i,j) = 1;
            discretization_->p(i,j) = 0;
        }
    }    
     std::cout << "+++++++++++++++++++++++++++++++++++" << std::endl;

    pressureSolver_->solve();
   for (int j = discretization_->pJEnd()-1; j >= discretization_->pJBegin(); j--)
     {
       std::cout << std::setw(fieldWidth) << j << "|";
       for (int i = discretization_->pIBegin(); i < discretization_->pIEnd(); i++)
       {
           std::cout<< std::setw(fieldWidth) << std::setprecision(fieldWidth-6) << discretization_->p(i,j);
       }
       std::cout << std::endl;
     }
     std::cout << std::endl;


     
}