#include "velext.h"
#include "sparse.h"


/* find boundary conds in list */
pCl getCl(pSol sol,int ref,int elt) {
  pCl     pcl;
  int     i;

  for (i=0; i<sol->nbcl; i++) {
    pcl = &sol->cl[i];
    if ( (pcl->ref == ref) && (pcl->elt == elt) )  return(pcl);
  }
  return(0);
}


/* retrieve physical properties in list */
int getMat(pSol sol,int ref,double *alpha) {
  pMat   pm;
  int    i;

  *alpha = sol->alpha;
  if ( sol->nmat == 0 )  return(1);

  for (i=0; i<sol->nmat; i++) {
    pm = &sol->mat[i];
    if ( pm->ref == ref ) {
      *alpha = pm->alpha;
      return(1);
    }
  }
  *alpha = VL_ALPHA;

  return(0);
}


/* triangle area */
static inline double area_2d(double *a,double *b,double *c) {
  double    ux,uy,vx,vy,dd;

  ux = b[0] - a[0];
  uy = b[1] - a[1];
  vx = c[0] - a[0];
  vy = c[1] - a[1];
  dd = 0.5 * (ux*vy - uy*vx);
  return(dd);
}


int invmat_2d(double m[4],double mi[4]) {
  double   det;

  det = m[0]*m[3] - m[2]*m[1];
  if ( fabs(det) < VL_EPSD )  return(0);
  det = 1.0 / det;
	mi[0] =  m[3]*det;
	mi[1] = -m[1]*det;
	mi[2] = -m[2]*det;
	mi[3] =  m[0]*det;

	return(1);
}


/* set TGV to diagonal coefficient when Dirichlet */
static int setTGV_2d(VLst *vlst,pCsr A) {
  pCl      pcl;
	pEdge    pa;
  pPoint   ppt;
  int      k,ig;
	char     i;

  /* Set Dirichlet's boundary for the state system */
  if ( vlst->sol.cltyp & VL_ver ) {
    /* at mesh nodes */
    for (k=1; k<=vlst->info.np; k++) {
      ppt = &vlst->mesh.point[k];
      pcl = getCl(&vlst->sol,ppt->ref,VL_ver);
      if ( pcl && pcl->typ == Dirichlet ) {
        /* set value for scalar or vector field */
        if ( vlst->info.ls ) {
          csrSet(A,k-1,k-1,VL_TGV);
        }
        else {
          ig = 2*(k-1);
          csrSet(A,ig+0,ig+0,VL_TGV);
          csrSet(A,ig+1,ig+1,VL_TGV);
        }
			}
    }
  }

	else if ( vlst->sol.cltyp & VL_edg )	{
    /* at mesh edges */
    for (k=1; k<=vlst->info.na; k++) {
      pa = &vlst->mesh.edge[k];
			pcl = getCl(&vlst->sol,pa->ref,VL_edg);
      if ( pcl && pcl->typ == Dirichlet ) {
        /* set value for scalar or vector field */
        if ( vlst->info.ls ) {
          for (i=0 ; i<2; i++) {
            ig = pa->v[i] - 1;
            csrSet(A,pa->v[i]-1,pa->v[i]-1,VL_TGV);
          }
        }
        else {
          for (i=0 ; i<2; i++) {
            ig = 2*(pa->v[i]-1);
            csrSet(A,ig+0,ig+0,VL_TGV);
            csrSet(A,ig+1,ig+1,VL_TGV);
          }
        }
      }
		}
	}

	return(1);
}


static pCsr matA1_2d(VLst *vlst) {
  pCsr     A;
  pTria    pt;
  pPoint   p0,p1,p2;
  double   Dp[3][2],m[4],im[4],Gr[3][2],alpha,vol,kij,term0,termG;
  int      nr,nc,nbe,k,ni,nj,il,ic;
  char     i,j;

	/* memory allocation (rough estimate) */
	nr  = nc = vlst->info.np;
  nbe = 10*vlst->info.np;
  A   = csrNew(nr,nc,nbe,CS_UT+CS_SYM);

  /* Dp */
  Dp[0][0] = -1.0; Dp[1][0] = 1.0; Dp[2][0] = 0.0;
  Dp[0][1] = -1.0; Dp[1][1] = 0.0; Dp[2][1] = 1.0; 

  /* Fill stiffness matrix of Laplace problem */
  for (k=1; k<=vlst->info.nt; k++) {
    pt = &vlst->mesh.tria[k];
    if ( !getMat(&vlst->sol,pt->ref,&alpha) )  continue;

    p0 = &vlst->mesh.point[pt->v[0]];
    p1 = &vlst->mesh.point[pt->v[1]];
    p2 = &vlst->mesh.point[pt->v[2]];

    m[0] = p1->c[0]-p0->c[0];  m[1] = p1->c[1]-p0->c[1];
    m[2] = p2->c[0]-p0->c[0];  m[3] = p2->c[1]-p0->c[1];
    if ( !invmat_2d(m,im) )  return(0);

    /* volume of element k */
    vol = area_2d(p0->c,p1->c,p2->c);

    /* Gradients of shape functions : Gr[i] = im*Dp[i] */
    for (i=0; i<3; i++) {
      Gr[i][0] = im[0]*Dp[i][0] + im[1]*Dp[i][1];
      Gr[i][1] = im[2]*Dp[i][0] + im[3]*Dp[i][1];
    }

    /* Flow local stiffness matrix into global one */
    for (i=0; i<3; i++) {
      for (j=i; j<3; j++) {
        if ( j==i )
          term0 = vol / 6.0;
        else
          term0 = vol / 12.0;

        termG = vol * (Gr[i][0]*Gr[j][0] + Gr[i][1]*Gr[j][1]);   
        kij = term0 + alpha * termG;
        ni  = pt->v[i]; 
        nj  = pt->v[j];
        if ( ni < nj ) {
          il = ni-1;
          ic = nj-1;
        }
        else {
          il = nj-1; 
          ic = ni-1;
        }
        csrPut(A,il,ic,kij);
      }
    }
  }

  setTGV_2d(vlst,A);
  csrPack(A);

  if ( vlst->info.verb == '+' )
    fprintf(stdout,"     %dx%d matrix, %.2f sparsity\n",nr,nc,100.0*A->nbe/nr/nc);

  return(A);
}


/* build stiffness matrix (vector case) */
static pCsr matA2_2d(VLst *vlst) {
  pCsr     A;
  pTria    pt;
  pPoint   p0,p1,p2;
  double   m[4],im[4],Gr[3][3],alpha,cof,vol,kij,term0,termG;
  int      nr,nc,nbe,k,ni,nj,il,ic;
  char     i,j;

  /* memory allocation (rough estimate) */
  nr  = nc = 2*vlst->info.np;
  nbe = 10*vlst->info.np;
  A   = csrNew(nr,nc,nbe,CS_UT+CS_SYM);

  /* Fill stiffness matrix of Laplace problem */
  for (k=1; k<=vlst->info.nt; k++) {
    pt = &vlst->mesh.tria[k];
    if ( !getMat(&vlst->sol,pt->ref,&alpha) )  continue;

    p0 = &vlst->mesh.point[pt->v[0]];
    p1 = &vlst->mesh.point[pt->v[1]];
    p2 = &vlst->mesh.point[pt->v[2]];
    
    m[0] = p1->c[0]-p0->c[0];  m[1] = p1->c[1]-p0->c[1];
    m[2] = p2->c[0]-p0->c[0];  m[3] = p2->c[1]-p0->c[1];
  
    /* volume of element k */
    vol = area_2d(p0->c,p1->c,p2->c);
    cof = 1.0 / (4.0*vol);
      
    Gr[0][0] = cof*(m[2]-m[0])*(m[2]-m[0])+ cof*(m[3]-m[1])*(m[3]-m[1]);
    Gr[1][1] = cof*(m[2]*m[2]+m[3]*m[3]);
    Gr[2][2] = cof*(m[0]*m[0]+m[1]*m[1]);
    Gr[0][1] = -cof*(m[2]-m[0])*(m[2])-cof*(m[3]-m[1])*(m[3]);
    Gr[0][2] = cof*(m[2]-m[0])*(m[0])+cof*(m[3]-m[1])*(m[1]);
    Gr[1][2] = -cof*(m[2])*(m[0])-cof*(m[3])*(m[1]);

    /* Flow local stiffness matrix into global one */
    for (i=0; i<3; i++) {
      for (j=i; j<3; j++) {
        if ( j==i )
          term0 = vol / 6.0;
        else
          term0 = vol / 12.0;
          
        /* termG = vol * (Gr[i][0]*Gr[j][0] + Gr[i][1]*Gr[j][1]); */
        termG = Gr[i][j];
        kij = term0 + alpha * termG;
        ni  = pt->v[i];
        nj  = pt->v[j];
        il  = 2*(ni-1);
        ic  = 2*(nj-1);
        if ( i == j ) {
          csrPut(A,il,ic,kij);
          csrPut(A,il+1,ic+1,kij);
        }
        else {
          csrPut(A,il,ic,kij);
          csrPut(A,ic,il,kij);
          csrPut(A,il+1,ic+1,kij);
          csrPut(A,ic+1,il+1,kij);
        }
      }
    }
  }

  setTGV_2d(vlst,A);
  csrPack(A);

  if ( vlst->info.verb == '+' )
    fprintf(stdout,"     %dx%d matrix, %.2f sparsity\n",nr,nc,100.0*A->nbe/nr/nc);

  return(A);
}


/* build right hand side vector and set boundary conds. */
static double *rhsF_2d(VLst *vlst) {
  pPoint   ppt;
  pCl      pcl;
  double  *F,*vp;
  int      k,ig,nb;

  if ( vlst->info.verb == '+' )  fprintf(stdout,"     body forces: ");
  if ( vlst->info.ls )
    F = (double*)calloc(vlst->info.np,sizeof(double));
  else
    F = (double*)calloc(2*vlst->info.np,sizeof(double));
  assert(F);

  /* nodal boundary conditions */
  nb = 0;
  if ( vlst->sol.cltyp & VL_ver ) {
	  for (k=1; k<=vlst->info.np; k++) {
	    ppt = &vlst->mesh.point[k];
      if ( !ppt->ref )  continue;
			pcl = getCl(&vlst->sol,ppt->ref,VL_ver);
	    if ( !pcl )  continue;
      nb++;
      if ( vlst->info.ls ) {
        vp = pcl->att == 'f' ? &vlst->sol.u[k-1] : &pcl->u[0];
        F[k-1] = VL_TGV * vp[0];
      }
      else {
        vp = pcl->att == 'f' ? &vlst->sol.u[2*(k-1)] : &pcl->u[0];
        F[2*(k-1)+0] = VL_TGV * vp[0];
        F[2*(k-1)+1] = VL_TGV * vp[1];
	    }
		}
	}
  if ( vlst->info.verb == '+' )  fprintf(stdout," %d conditions assigned\n",nb);

	return(F);
}


/* solve Helmholz */
int velex1_2d(VLst *vlst) {
  pCsr     A;
  double  *F,err;
  int      nit,ier;
  char     stim[32];

  /* -- Part I: matrix assembly */
  if ( vlst->info.verb != '0' )  fprintf(stdout,"    Matrix and right-hand side assembly\n");
	
  /* build matrix */
  A = vlst->info.ls ? matA1_2d(vlst) : matA2_2d(vlst);
  F = rhsF_2d(vlst);

  /* free mesh structure + boundary conditions */
  if ( vlst->info.mfree ) {
		free(vlst->mesh.tria);
    if ( !vlst->info.zip )  free(vlst->mesh.point);
	}

  /* -- Part II: Laplace problem solver */
  if ( vlst->info.verb != '0' ) {
    fprintf(stdout,"    Solving linear system:");  fflush(stdout);
    ier = csrPrecondGrad(A,vlst->sol.u,F,&vlst->sol.err,&vlst->sol.nit,1);
    if ( ier <= 0 )
      fprintf(stdout,"\n # convergence problem: %d\n",ier);
    else
      fprintf(stdout," %E in %d iterations\n",vlst->sol.err,vlst->sol.nit);
	}
  else {
    ier = csrPrecondGrad(A,vlst->sol.u,F,&vlst->sol.err,&vlst->sol.nit,1);
  }

  /* free memory */
  csrFree(A);
	free(F);

  return(ier > 0);  
}

