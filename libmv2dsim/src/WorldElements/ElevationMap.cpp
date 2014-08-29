/*+-------------------------------------------------------------------------+
  |                       MultiVehicle 2D simulator (libmv2dsim)            |
  |                                                                         |
  | Copyright (C) 2014  Jose Luis Blanco Claraco (University of Almeria)    |
  | Distributed under GNU General Public License version 3                  |
  |   See <http://www.gnu.org/licenses/>                                    |
  +-------------------------------------------------------------------------+  */

#include <mv2dsim/WorldElements/ElevationMap.h>
#include <mv2dsim/World.h>
#include <mv2dsim/VehicleBase.h>
#include "xml_utils.h"

#include <mrpt/opengl/COpenGLScene.h>
//#include <mrpt/system/filesystem.h>
//#include <mrpt/utils/CObject.h>
#include <rapidxml.hpp>

using namespace rapidxml;
using namespace mv2dsim;
using namespace std;


ElevationMap::ElevationMap(World*parent,const rapidxml::xml_node<char> *root) :
	WorldElementBase(parent),
	m_first_scene_rendering(true),
	m_resolution(1.0)
{
	loadConfigFrom(root);
}

ElevationMap::~ElevationMap()
{
}

void ElevationMap::loadConfigFrom(const rapidxml::xml_node<char> *root)
{
	// Other general params:
	std::map<std::string,TParamEntry> params;
	
	std::string sElevationImgFile;
	params["elevation_image"] = TParamEntry("%s", &sElevationImgFile);
	std::string sTextureImgFile;
	params["texture_image"] = TParamEntry("%s", &sTextureImgFile);	

	double img_min_z=0.0, img_max_z=5.0;
	params["elevation_image_min_z"] = TParamEntry("%lf", &img_min_z);
	params["elevation_image_max_z"] = TParamEntry("%lf", &img_max_z);
	
	mrpt::utils::TColor mesh_color;
	params["mesh_color"] = TParamEntry("%color", &mesh_color);

	params["resolution"] = TParamEntry("%lf", &m_resolution);
	
	parse_xmlnode_children_as_param(*root,params);


	// Load elevation data:
	mrpt::math::CMatrixFloat elevation_data;
	if (!sElevationImgFile.empty())
	{
		mrpt::utils::CImage imgElev;
		if (!imgElev.loadFromFile(sElevationImgFile,0 /*force load grayscale*/))
			throw std::runtime_error(mrpt::format("[ElevationMap] ERROR: Cannot read elevation image '%s'",sElevationImgFile.c_str()));

		// Scale: [0,1] => [min_z,max_z]
		imgElev.getAsMatrix(elevation_data);  // Get image normalized in range [0,1]
		ASSERT_(img_min_z!=img_max_z)
		elevation_data.adjustRange(img_min_z,img_max_z);
	}
	else
	{
		MRPT_TODO("Imgs or txt matrix")
	}

	// Load texture (optional):
	mrpt::utils::CImage mesh_image;
	bool has_mesh_image = false;
	if (!sTextureImgFile.empty())
	{
		if (!mesh_image.loadFromFile(sTextureImgFile))
			throw std::runtime_error(mrpt::format("[ElevationMap] ERROR: Cannot read texture image '%s'",sTextureImgFile.c_str()));
		has_mesh_image=true;
	}

	// Build mesh:
	m_gl_mesh = mrpt::opengl::CMesh::Create();

	m_gl_mesh->enableTransparency(false);

	if (has_mesh_image)
	{
		ASSERT_EQUAL_(mesh_image.getWidth(),(size_t)elevation_data.cols())
		ASSERT_EQUAL_(mesh_image.getHeight(),(size_t)elevation_data.rows())
		m_gl_mesh->assignImageAndZ(mesh_image, elevation_data);
	}
	else
	{
		m_gl_mesh->setZ(elevation_data);
		m_gl_mesh->setColor_u8(mesh_color);
	}

	// Save copy for calcs:
	m_mesh_z_cache = elevation_data;
	
	// Extension: X,Y
	const double LX = elevation_data.cols() * m_resolution;
	const double LY = elevation_data.rows() * m_resolution;
	m_gl_mesh->setGridLimits(-0.5*LX, 0.5*LX, -0.5*LY, 0.5*LY);
		
}

void ElevationMap::gui_update( mrpt::opengl::COpenGLScene &scene)
{
	using namespace mrpt::math;

	ASSERTMSG_(m_gl_mesh, "ERROR: Can't render Mesh before loading it! Have you called loadConfigFrom() first?")

	// 1st time call?? -> Create objects
	if (m_first_scene_rendering)
	{
		m_first_scene_rendering=false;
		SCENE_INSERT_Z_ORDER(scene,0, m_gl_mesh);
	}

}

void ElevationMap::simul_pre_timestep(const TSimulContext &context)
{
	// For each vehicle:
	// 1) Compute its 3D pose according to the mesh tilt angle.
	// 2) Apply gravity force

	ASSERT_(m_gl_mesh)
	const mrpt::opengl::CMesh * mesh = m_gl_mesh.pointer();

	const World::TListVehicles & lstVehs =  this->m_world->getListOfVehicles();
	for (World::TListVehicles::const_iterator itVeh=lstVehs.begin();itVeh!=lstVehs.end();++itVeh)
	{
		// 1) Compute its 3D pose according to the mesh tilt angle.
		// -------------------------------------------------------------
		const mrpt::math::TPose3D &cur_pose = itVeh->second->getPose();

		mrpt::math::TPose3D new_pose = cur_pose;
		
		float z;
		if (!getElevationAt(cur_pose.x,cur_pose.y,z))
			continue; // vehicle is out of bounds!

		new_pose.z = z;

		itVeh->second->setPose(new_pose);

		// 2) Apply gravity force
		// -------------------------------------------------------------
		//it_Veh->apply_force(double fx, double fy, double local_ptx, double local_pty)
	}
}

void ElevationMap::simul_post_timestep(const TSimulContext &context)
{
	MRPT_TODO("Save all elements positions in prestep, then here scale their movements * cos(angle)")

}
float calcz(
	const mrpt::math::TPoint3Df &p1, 
	const mrpt::math::TPoint3Df &p2,
	const mrpt::math::TPoint3Df &p3, 
	float x, float y) 
{
	const float det = (p2.x - p3.x) * (p1.y - p3.y) + (p3.y - p2.y) * (p1.x - p3.x);
	ASSERT_(det!=0.0f)
	
	const float l1 = ((p2.x - p3.x) * (y - p3.y) + (p3.y - p2.y) * (x - p3.x)) / det;
	const float l2 = ((p3.x - p1.x) * (y - p3.y) + (p1.y - p3.y) * (x - p3.x)) / det;
	const float l3 = 1.0f - l1 - l2;
	
	return l1 * p1.z + l2 * p2.z + l3 * p3.z;
}


bool ElevationMap::getElevationAt(double x,double y, float &z) const
{
	const mrpt::opengl::CMesh * mesh = m_gl_mesh.pointer();
	const float x0 = mesh->getXMin();
	const float y0 = mesh->getYMin();
	const size_t nCellsX = m_mesh_z_cache.rows();
	const size_t nCellsY = m_mesh_z_cache.cols();
	
	// Discretize:
	const int cx00 = ::floor((x-x0)/m_resolution);
	const int cy00 = ::floor((y-y0)/m_resolution);
	if (cx00<1 || cx00>=nCellsX-1 || cy00<1 || cy00>=nCellsY-1) 
		return false;
	
	// Linear interpolation:
	const float z00 = m_mesh_z_cache(cx00,cy00);
	const float z01 = m_mesh_z_cache(cx00,cy00+1);
	const float z10 = m_mesh_z_cache(cx00+1,cy00);
	const float z11 = m_mesh_z_cache(cx00+1,cy00+1);

	// 
	//   p01 ---- p11
	//    |        |
	//   p00 ---- p10
	//
	const mrpt::math::TPoint3Df p00(.0f, .0f, z00);
	const mrpt::math::TPoint3Df p01(.0f, m_resolution, z01);
	const mrpt::math::TPoint3Df p10(m_resolution, .0f, z10);
	const mrpt::math::TPoint3Df p11(m_resolution, m_resolution, z11);

	const float lx = x-(x0+cx00*m_resolution);
	const float ly = y-(y0+cy00*m_resolution);

	if (ly>=lx)
	     z = calcz(p00,p01,p11, ly,ly);
	else z = calcz(p00,p10,p11, ly,ly);

	return true; 
}