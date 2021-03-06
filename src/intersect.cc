//
//// Copyright (c) 2016 CNRS
//// Authors: Anna Seppala
////
//// This file is part of hpp-intersect
//// hpp-intersect is free software: you can redistribute it
//// and/or modify it under the terms of the GNU Lesser General Public
//// License as published by the Free Software Foundation, either version
//// 3 of the License, or (at your option) any later version.
////
//// hpp-intersect is distributed in the hope that it will be
//// useful, but WITHOUT ANY WARRANTY; without even the implied warranty
//// of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//// General Lesser Public License for more details.  You should have
//// received a copy of the GNU Lesser General Public License along with
//// hpp-intersect  If not, see
//// <http://www.gnu.org/licenses/>.
//
//
#include <hpp/intersect/intersect.hh>
#include <hpp/intersect/geom/algorithms.h>
#include <hpp/fcl/collision.h>
#include <limits>
#include <boost/math/special_functions/sign.hpp>

namespace hpp {
    namespace intersect {

        // helper class to save triangle vertex positions in world frame
        struct TrianglePoints
        {   
            fcl::Vec3f p1, p2, p3; 
        };

        std::vector<double> getRadius (const Eigen::VectorXd& params,
                Eigen::Vector2d& centroid, double& tau)
        {
          // if number of parameters == 5 -->assume it's a circle ?
          // if number == 6 --> ellipse
          std::vector<double> res;
          bool ellipse = true;
          if (params.size () < 6) {
              std::ostringstream oss
                ("getRadius: Wrong number of parameters in conic function!!.");
              throw std::runtime_error (oss.str ());
          // if the coefficient B of eq. Ax^2 + Bxy + Cy^2 + Dx + Ey + F = 0 is zero,
          // the function describes a circle
          } else if (params(1) == 0.0) {
              ellipse = false;
          }
          
          std::vector<double> radii;
          if (ellipse) {
              double A(params(0)), B(params(1)), C(params(2)), D(params(3)),
                     E(params(4)), F(params(5));

              Eigen::Matrix3d M0;
              M0 << F, D/2.0, E/2.0, D/2.0, A, B/2.0, E/2.0, B/2.0, C;
              Eigen::Matrix2d M;
              M << A, B/2.0, B/2.0, C;
              Eigen::EigenSolver<Eigen::Matrix2d> es(M);
              Eigen::EigenSolver<Eigen::Matrix2d>::EigenvalueType eval = es.eigenvalues ();
              Eigen::Vector2d lambda;

              // make sure eigenvalues are in order for the rest of the computations
              if (fabs(eval(0).real () - A) > fabs(eval(0).real () - C)) {
                 lambda << eval(1).real (), eval(0).real ();   
              } else {
                 lambda << eval(0).real (), eval(1).real ();
              }
              radii.push_back (sqrt (-M0.determinant ()/(M.determinant () * lambda(0))));
              radii.push_back (sqrt (-M0.determinant ()/(M.determinant () * lambda(1))));
              res = radii;
              centroid << (B*E - 2*C*D)/(4*A*C - B*B), (B*D - 2*A*E)/(4*A*C - B*B);
              tau = (atan(B/(A-C)))/2.0;
              // tau is always the rotation angle when the longer radius lies along the X axis
              // in the original (tau == 0) position
              if (radii[0] < radii[1]) {
                  tau = tau - M_PI/2.0;
              }
             
          } else { //circle!
              centroid << params(3)/(-2.0), params(4)/(-2.0);
              res.push_back (sqrt (centroid(0)*centroid(0) + centroid(1)*centroid(1) - params(5)));
              tau = 0.0; 
          }
          return res;
        }

        Eigen::VectorXd directEllipse(const std::vector<Eigen::Vector3d>& points)
        {
          const size_t nPoints = points.size ();
          // only consider x and y coordinates: supposing points are in a plane
          Eigen::MatrixXd XY(nPoints,2);
          // TODO: optimise
          for (unsigned int i = 0; i < nPoints; ++i) {
              XY (i,0) = points[i][0];
              XY (i,1) = points[i][1];
          }
         
          Eigen::Vector2d centroid;
          centroid << XY.block(0,0, nPoints,1).mean (),
                  XY.block(0,1, nPoints, 1).mean ();

          Eigen::MatrixXd D1 (nPoints,3);
          D1 << (XY.block (0,0, nPoints,1).array () - centroid(0)).square (),
             (XY.block (0,0, nPoints,1).array () - centroid(0))*(XY.block (0,1, nPoints,1).array () - centroid(1)),
             (XY.block (0,1, nPoints,1).array () - centroid(1)).square ();
          Eigen::MatrixXd D2 (nPoints,3);
          D2 << XY.block (0,0, nPoints,1).array () - centroid(0),
             XY.block (0,1, nPoints,1).array () - centroid(1),
             Eigen::MatrixXd::Ones (nPoints,1);

          Eigen::Matrix3d S1 = D1.transpose () * D1;
          Eigen::Matrix3d S2 = D1.transpose () * D2;
          Eigen::Matrix3d S3 = D2.transpose () * D2;

          Eigen::Matrix3d T = -S3.inverse () * S2.transpose ();
          Eigen::Matrix3d M_orig = S1 + S2 * T;
          Eigen::Matrix3d M; M.setZero ();
          M << M_orig.block<1,3>(2,0)/2, -M_orig.block<1,3>(1,0), M_orig.block<1,3>(0,0)/2;

          Eigen::EigenSolver<Eigen::Matrix3d> es(M);
          Eigen::EigenSolver<Eigen::Matrix3d>::EigenvectorsType evecCplx = es.eigenvectors ();

          Eigen::Matrix3d evec = evecCplx.real ();

          Eigen::VectorXd cond (3);
          // The condition has the form 4xz - y^2 > 0 (infinite elliptic cone) for all
          // three eigen vectors. If none of the eigen vectors fulfils the inequality,
          // the direct ellipse method fails.
          cond = (4*evec.block<1,3>(0,0).array () * evec.block<1,3>(2,0).array () -
              evec.block<1,3>(1,0).array ().square ()).transpose ();
          
          Eigen::MatrixXd A0 (0,0);
          // TODO: A0 should always be of size 3x1 --> fix
          for (unsigned int i = 0; i < cond.size (); ++i) {
              if (cond(i) > 0.0) {
                  A0.resize (3,i+1);
                  A0.block(0,i,3,1) = evec.block<3,1>(0,i);
              }
          }
          if (A0.size () < 3) {
              std::ostringstream oss
              ("intersect::directEllipse: Could not create ellipse approximation. Maybe try circle instead?");
            throw std::runtime_error (oss.str ());
          }
          // A1.rows () + T.rows () should always be equal to 6!!
          Eigen::MatrixXd A(A0.rows () + T.rows (), A0.cols ());
          A.block(0,0,A0.rows (), A0.cols ()) = A0;
          A.block(A0.rows (), 0, T.rows (), A0.cols ()) = T*A0;

          double A3 = A(3,0) - 2*A(0,0) * centroid(0) - A(1,0) * centroid(1);
          double A4 = A(4,0) - 2*A(2,0) * centroid(1) - A(1,0) * centroid(0);
          double A5 = A(5,0) + A(0,0) * centroid(0)*centroid(0) + A(2,0) * centroid(1)*centroid(1) +
              A(1,0) * centroid(0) * centroid(1) - A(3,0) * centroid(0) - A(4,0) * centroid(1);

          A(3,0) = A3;  A(4,0) = A4;  A(5,0) = A5;
          A = A/A.norm ();

          return A.block<6,1>(0,0);

        }

        Eigen::VectorXd directCircle (const std::vector<Eigen::Vector3d>& points)
        {
          const size_t nPoints = points.size ();
          // only consider x and y coordinates: supposing points are in a plane
          Eigen::MatrixXd XY(nPoints,2);
          // TODO: optimise
          for (unsigned int i = 0; i < nPoints; ++i) {
              XY (i,0) = points[i][0];
              XY (i,1) = points[i][1];
          }
         
          Eigen::Vector2d centroid;
          centroid << XY.block(0,0, nPoints,1).mean (),
                  XY.block(0,1, nPoints, 1).mean ();

          double radius = (((XY.block(0,0, nPoints, 1).array () - centroid(0)).square () + 
                  (XY.block(0,1, nPoints, 1).array () -centroid(1)).square ()).sqrt ()).mean ();

          std::cout << "circle radius: " << radius << std::endl;

          Eigen::VectorXd params (6);
          params << 1.0, 0.0, 1.0, -2*centroid(0), -2*centroid(1), centroid(0)*centroid(0) +
              centroid(1)*centroid(1) - radius*radius;

          return params;

        }
        
        Eigen::Vector3d projectToPlane (std::vector<Eigen::Vector3d> points, Eigen::Vector3d& planeCentroid)
        {
          if (points.size () < 3) {
   					std::ostringstream oss
              ("projectToPlane: Too few input points to create plane.");
            throw std::runtime_error (oss.str ());
          }
          
          const size_t nPoints = points.size ();
          Eigen::MatrixXd XYZ (nPoints,3);
          Eigen::MatrixXd XYZ0 (nPoints,3);
          Eigen::VectorXd b (nPoints);
          // TODO: optimise
          for (unsigned int i = 0; i < nPoints; ++i) {
              XYZ (i,0) = points[i][0];
              XYZ (i,1) = points[i][1];
              XYZ (i,2) = points[i][2];
          }
          Eigen::Vector3d cm (XYZ.block (0,0,nPoints,1).mean (), 
                 XYZ.block (0,1,nPoints,1).mean (), XYZ.block (0,2,nPoints,1).mean ());
          XYZ0 = XYZ - (Eigen::MatrixXd::Ones (nPoints,1) * cm.transpose ());

          Eigen::EigenSolver<Eigen::Matrix3d> es(XYZ0.transpose () * XYZ0);
          Eigen::EigenSolver<Eigen::Matrix3d>::EigenvectorsType evecCplx = es.eigenvectors ();
          Eigen::EigenSolver<Eigen::Matrix3d>::EigenvalueType eval = es.eigenvalues ();

          // find index of smallest eigen vector: this is the index of the normal vector.
          unsigned int index = 0;
          Eigen::VectorXd eigval (eval.real ());
          for(unsigned int i = 1; i < eigval.size (); ++i)
          {
              if(eigval (i) < eigval (index)) {
                  index = i;
              }
          }
          Eigen::MatrixXd evec = evecCplx.real ();
          Eigen::Vector3d normal = evec.block(0,index,3,1);

          normal.normalize ();
          
          planeCentroid = cm;
          Eigen::Matrix3d origin;
          origin.setZero ();
          origin.diagonal () = planeCentroid;

          Eigen::MatrixXd distance (nPoints,3);

          distance = XYZ - (Eigen::MatrixXd::Ones (nPoints,3)*origin);


          // scalar distance from point to plane along the normal for all points in vector
          Eigen::VectorXd scalarDist (nPoints);
          scalarDist = distance.block(0,0,nPoints,1)*normal(0) + distance.block(0,1,nPoints,1)*normal(1) +
              distance.block(0,2,nPoints,1)*normal(2);
  
          // TODO: optimise
          for (unsigned int i = 0; i > points.size (); ++i) {
            Eigen::Vector3d projectecPoint = XYZ.block(i,0, 1,3).transpose () - scalarDist(i)*normal;
            points[i][0] = projectecPoint (0);
            points[i][1] = projectecPoint (1);
            points[i][2] = projectecPoint (2);
          }

          return normal;
        }

        // Helper function to get the underlying model of fcl::CollisionObject. Allows
        // access to mesh triangles and their vertices.
        BVHModelOBConst_Ptr_t GetModel (const fcl::CollisionObjectConstPtr_t& object)
        {   
            assert (object->collisionGeometry ()->getNodeType () == fcl::BV_OBBRSS);
            const BVHModelOBConst_Ptr_t model = boost::static_pointer_cast<const BVHModelOB>
                                                (object->collisionGeometry ());
            assert (model->getModelType () == fcl::BVH_MODEL_TRIANGLES);
            return model;
        }

        // A Fast Triangle-Triangle Intersection Test by Tomas M�ller
        std::vector<Eigen::Vector3d> TriangleIntersection (const TrianglePoints& rom, const TrianglePoints& aff)
        {
         //plane equation C(0)x + C(1)y + C(2)z + C3 = 0
         Eigen::Vector3d romC;
         double romC3;
         Eigen::Vector3d affC;
         double affC3;
         std::vector<Eigen::Vector3d> res;
         double X (0.0);
         double Y (0.0);
         double Z (0.0);

         romC << (rom.p2 - rom.p1).cross (rom.p3 - rom.p1);
         //romC.normalize ();
         romC3 = (-romC).dot (rom.p1);

         // signed distances from the vertices of aff to the plane of rom
         // (multiplied by a constant romC.block(0,0,3,1) dot romC.block(0,0,3,1))
         Eigen::Vector3d a2r (romC.dot(aff.p1) + romC3,
                 romC.dot(aff.p2) + romC3,
                 romC.dot(aff.p3) + romC3);
         // if all distances have the same sign and are not zero, no overlap exists
         if ((a2r[0] < 0 && a2r[1] < 0 && a2r[2] < 0) || (a2r[0] > 0 && a2r[1] > 0 && a2r[2] > 0)) {
            res.clear ();
            return res;// return empty vector;
         }

         //same procedure needed for affC
         affC << (aff.p2 - aff.p1).cross (aff.p3 - aff.p1);
         //affC.normalize ();
         affC3 = (-affC).dot (aff.p1);

         Eigen::Vector3d r2a (affC.dot(rom.p1) + affC3,
                 affC.dot(rom.p2) + affC3,
                 affC.dot(rom.p3) + affC3);
         if ((r2a[0] < 0 && r2a[1] < 0 && r2a[2] < 0) || (r2a[0] > 0 && r2a[1] > 0 && r2a[2] > 0)) {
            res.clear ();
            return res;
         }

        // if we get this far, triangles intersect or are coplanar
        if (r2a.isZero (1e-6)) {
            // TODO: 2D convex hull
            // deal with coplanar triangles and return?
        }
        // The intersection of aff and rom planes is a line L = p +tD,
        // D = affC.cross(romC) and p is a point on the line
        Eigen::Vector3d D = affC.cross(romC);
        D.normalize ();
        // if the intersection line is horizontal (either of the triangles
        // has a normal with only a Z component), the Z component cannot be arbitrarily
        // set to 0 to find a point on the line.
        if (fabs (D[2]) < 1e-6) {
           if (affC.block<2,1>(0,0).isZero(1e-6)) {
               Z = -affC3/affC[2];
               Y = 0.0; // take Y as 0 arbitrarily
               X = ((affC[2]-romC[2])*Z - romC[1]*Y + affC3 - romC3)/romC[0];
           } else if (romC.block<2,1>(0,0).isZero(1e-6)) {
               Z = -romC3/romC[2];
               Y = 0.0;
               X = ((romC[2]-affC[2])*Z - affC[1]*Y + romC3 - affC3)/affC[0];
           } else if (fabs (affC[1]) < 1e-6 && fabs (romC[1]) < 1e-6) { // D only in Y-direction
              Y = 0.0;
              X = (romC3 -affC3*(romC[2]/affC[2]))/(affC[0]*(romC[2]/affC[2]) -romC[0]);
              Z = (-affC[0]*X -affC3)/affC[2];
           } else {  // D only in X
              X = 0.0;
              Y = (romC3 -affC3*(romC[2]/affC[2]))/(affC[1]*(romC[2]/affC[2]) -romC[1]);
              Z = (-affC[1]*Y -affC3)/affC[2];
           }
        } else {
            Z = 0.0;
            if (fabs (affC[0]) < 1e-6) {
                X = ((affC[2]*romC[1] -romC[2]*affC[1])*Z +
                  affC3*romC[1] - romC3*affC[1])/ (romC[0]*affC[1] - affC[0]*romC[1]);
                Y = (-affC[0]*X -affC[2]*Z -affC3)/affC[1];
            } else {
                Y = ((affC[2]*romC[0] -romC[2]*affC[0])*Z +
                  affC3*romC[0] - romC3*affC[0])/ (romC[1]*affC[0] - affC[1]*romC[0]);
                X = (-affC[1]*Y - affC[2]*Z - affC3)/ affC[0];
            }
        }
        // point on intersecting line
        Eigen::Vector3d p(X,Y,Z);
 
       // Now find scalar interval along L that represents the intersection
       // between affordance Triangle and L
       Eigen::Vector3d projected;
       Eigen::Vector3d dist;
       projected[0] = (D.dot((aff.p1-p)));
       dist[0] = a2r[0];
       if (boost::math::sign (a2r[0]) == boost::math::sign(a2r[1])) {
          projected[0] = (D.dot((aff.p1-p)));
          dist[0] = a2r[0];
          projected[1] = (D.dot((aff.p3-p))); // different sign
          projected[2] = (D.dot((aff.p2-p)));
          dist[1] = a2r[2];
          dist[2] = a2r[1];
       } else if (boost::math::sign (a2r[0]) == boost::math::sign(a2r[2])) {
          projected[0] = (D.dot((aff.p1-p)));
          dist[0] = a2r[0];
          projected[1] = (D.dot((aff.p2-p))); // different sign
          projected[2] = (D.dot((aff.p3-p)));
          dist[1] = a2r[1];
          dist[2] = a2r[2];
       } else {
          projected[0] = (D.dot((aff.p2-p)));
          dist[0] = a2r[1];
          projected[1] = (D.dot((aff.p1-p))); // different sign
          projected[2] = (D.dot((aff.p3-p)));
          dist[1] = a2r[0];
          dist[2] = a2r[2];
       }        

        Eigen::Vector2d afft;
        afft[0] = projected[0] + (projected[1] -projected[0])*(dist[0])/(dist[0]-dist[1]);
        afft[1] = projected[1] + (projected[2] -projected[1])*(dist[1])/(dist[1]-dist[2]);

       // same for rom triangle:
       projected.setZero();
       dist.setZero();

       // rearrange points so that projected[1] is the point on the other side of the line
       if (boost::math::sign (r2a[0]) == boost::math::sign(r2a[1])) {
          projected[0] = (D.dot((rom.p1-p)));
          dist[0] = r2a[0];
          projected[1] = (D.dot((rom.p3-p))); // different sign
          projected[2] = (D.dot((rom.p2-p)));
          dist[1] = r2a[2];
          dist[2] = r2a[1];
       } else if (boost::math::sign (r2a[0]) == boost::math::sign(r2a[2])) {
          projected[0] = (D.dot((rom.p1-p)));
          dist[0] = r2a[0];
          projected[1] = (D.dot((rom.p2-p))); // different sign
          projected[2] = (D.dot((rom.p3-p)));
          dist[1] = r2a[1];
          dist[2] = r2a[2];
       } else {
          projected[0] = (D.dot((rom.p2-p)));
          dist[0] = r2a[1];
          projected[1] = (D.dot((rom.p1-p))); // different sign
          projected[2] = (D.dot((rom.p3-p)));
          dist[1] = r2a[0];
          dist[2] = r2a[2];
       }
       Eigen::Vector2d romt;
       romt[0] = projected[0] + (projected[1] -projected[0])*(dist[0])/(dist[0]-dist[1]);
       romt[1] = projected[1] + (projected[2] -projected[1])*(dist[1])/(dist[1]-dist[2]);
       
        if ((std::min(afft[0], afft[1]) < std::max (romt[0], romt[1])) && 
                (std::min (afft[0], afft[1]) > std::min (romt[0], romt[1])) ||
                (std::min (romt[0], romt[1]) < std::max (afft[0], afft[1])) &&
                (std::min (romt[0], romt[1]) > std::min (afft[0], afft[1]))) {
            double t1 = std::max (std::min (afft[0], afft[1]), std::min (romt[0], romt[1]));
            double t2 = std::min (std::max (afft[0], afft[1]), std::max (romt[0], romt[1]));
            res.push_back(p + D*(t1));
            res.push_back(p + D*(t2));
        }
        return res;
        }

        Inequality fcl2inequalities (const fcl::CollisionObjectPtr_t& rom)
        {
          BVHModelOBConst_Ptr_t romModel (GetModel (rom));
          Eigen::MatrixXd A(romModel->num_tris, 3);
          Eigen::VectorXd b(romModel->num_tris);
          Eigen::MatrixXd N(romModel->num_tris, 3);
          Eigen::MatrixXd V = Eigen::MatrixXd::Ones(romModel->num_tris, 4);

          TrianglePoints tri; // to save world position of vertices in matrix form
          Eigen::Matrix3d vertexNormals; // vertex normals are equal to triangle normal in this case
          for (unsigned int k = 0; k < romModel->num_tris; ++k) {
              fcl::Triangle fcltri = romModel->tri_indices[k]; 
              tri.p1 = rom->getRotation() * romModel->vertices[fcltri[0]] + rom->getTranslation(),
              tri.p2 = rom->getRotation() * romModel->vertices[fcltri[1]] + rom->getTranslation(),
              tri.p3 = rom->getRotation() * romModel->vertices[fcltri[2]] + rom->getTranslation();
              Eigen::Vector3d normal = (tri.p2 - tri.p1).cross (tri.p3 - tri.p1);

              A.block(k,0, 1,3) = normal.transpose ();
              b(k) = normal.dot (tri.p1);
              V.block(k,0, 1,3) = (Eigen::Vector3d (tri.p1)).transpose ();
              N.block(k,0, 1,3) = normal.transpose ();
          }

          Inequality ineq (A,b,N,V);
          return ineq;
        }

        bool is_inside (const Inequality& ineq, const Eigen::Vector3d point)
        {
          // TODO: more efficient way of testing the inequality? No loops.
          Eigen::VectorXd eq = ineq.A_ * point - ineq.b_;
          for (unsigned int k = 0; k <eq.size (); ++k) {
            if (eq(k) > 0.0) {
              return false;
            }
          }
          return true;
        }

        // custom funciton to get intersection points: not optimal time. 
        std::vector<Eigen::Vector3d> getIntersectionPoints (const fcl::CollisionObjectPtr_t& rom,
               const fcl::CollisionObjectPtr_t& affordance)
        {
          std::vector<Eigen::Vector3d> res;
          res.clear ();
          BVHModelOBConst_Ptr_t romModel (GetModel (rom));
          BVHModelOBConst_Ptr_t affModel (GetModel (affordance));

          TrianglePoints tri;
          std::vector<TrianglePoints> affTris; // triangles in world frame
          std::vector<TrianglePoints> romTris;
          
          for (unsigned int k = 0; k < affModel->num_tris; ++k) {
              fcl::Triangle fcltri = affModel->tri_indices[k];
              tri.p1 = affordance->getRotation() * affModel->vertices[fcltri[0]] + affordance->getTranslation();
              tri.p2 = affordance->getRotation() * affModel->vertices[fcltri[1]] + affordance->getTranslation();
              tri.p3 = affordance->getRotation() * affModel->vertices[fcltri[2]] + affordance->getTranslation();

              affTris.push_back (tri);
          }
          for (unsigned int k = 0; k < romModel->num_tris; ++k) {
              fcl::Triangle fcltri = romModel->tri_indices[k];
              tri.p1 = rom->getRotation() * romModel->vertices[fcltri[0]] + rom->getTranslation();
              tri.p2 = rom->getRotation() * romModel->vertices[fcltri[1]] + rom->getTranslation();
              tri.p3 = rom->getRotation() * romModel->vertices[fcltri[2]] + rom->getTranslation();

              romTris.push_back (tri); // only save tris that could be in contact with aff
          }
          Inequality ineq = fcl2inequalities (rom);
          for (unsigned int afftri = 0; afftri < affTris.size (); ++afftri) {
              // there are a lot of cases where internal points are found but are not the end points of aff
              // --> these are eliminated by taking the convex hull of found points.
              if (is_inside (ineq, affTris[afftri].p1)) {
                  res.push_back(Eigen::Vector3d(affTris[afftri].p1));
                  }                
              if (is_inside (ineq, affTris[afftri].p2)) {
                  res.push_back(Eigen::Vector3d(affTris[afftri].p2));
                  }
              if (is_inside (ineq, affTris[afftri].p3)) {
                  res.push_back(Eigen::Vector3d(affTris[afftri].p3));
                  }
          }
          // Check collision only after finding internal aff vertices: if the whole of aff
          // is within the ROM body, no collision will be found but the whole aff area is in fact available
          // for contact planning.
          CollisionPair_t col = CollisionPair_t (affordance, rom);
          fcl::CollisionRequest req;
          req.enable_contact = true;
          fcl::CollisionResult result;
          fcl::collide (col.first.get (), col.second.get (), req, result);
          if (!result.isCollision () && res.size () == 0) {
              std::cout << "ROM and affordance object not in collision!" << std::endl;
              res.clear ();
              return res;
          }

          for (unsigned int afftri = 0; afftri < affTris.size (); ++afftri) {
              for (unsigned int romtri = 0; romtri < romTris.size(); ++romtri) {
                  // check whether affTris[afftri] and romTris[romTri] intersect.
                  // If yes, find intersection line
                  std::vector<Eigen::Vector3d> points = TriangleIntersection (romTris[romtri],
                          affTris[afftri]);
                  res.insert(res.end(), points.begin(), points.end());
              }
          }
         // After finding points, create convex hull and refine to get more points for ellipse approximation
         std::vector<Eigen::Vector3d> hull = geom::convexHull<std::vector<Eigen::Vector3d> >(res.begin(), res.end());
         if (hull.size () > 2) {
            double minDist = 0.1; //10 cm minimum interval TODO: hard-coded or user-given value?
            for (unsigned int k = 0; k < hull.size () -1; ++k) {
                     if (minDist > (hull[k+1] - hull[k]).norm () && (hull[k+1] - hull[k]).norm () > 0.01) {
                  minDist = (hull[k+1] - hull[k]).norm ();
                }
            }
            std::vector<Eigen::Vector3d> hullRefined;
            for (unsigned int j = 0; j < hull.size () -1; ++j) {
                double intervals = std::ceil (((hull[j+1] - hull[j]).norm ())/minDist);
                for (unsigned int i = 0; i < (unsigned int) intervals; ++i) {
                    hullRefined.push_back (hull[j] + (i+1)*(hull[j+1]-hull[j])/intervals);
            }
         }
         res = hullRefined;
         }
          return res; 
        }

    } // namespace intersect
} // namespace hpp
