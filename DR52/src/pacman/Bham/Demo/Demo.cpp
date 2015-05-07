#include <pacman/Bham/Demo/Demo.h>

#include <Golem/Math/Rand.h>
#include <Grasp/Grasp/Model.h>
#include <Grasp/Data/Image/Image.h>
#include <Grasp/Core/Import.h>
#include <Grasp/App/Player/Data.h>
#include <Golem/Phys/Data.h>

using namespace pacman;
using namespace golem;
using namespace grasp;

//-----------------------------------------------------------------------------

namespace {
std::string toXMLString(const golem::Mat34& m)
{
	char buf[BUFSIZ], *begin = buf, *const end = buf + sizeof(buf) - 1;
	golem::snprintf(begin, end,
			"m11=\"%f\" m12=\"%f\" m13=\"%f\" m21=\"%f\" m22=\"%f\" m23=\"%f\" m31=\"%f\" m32=\"%f\" m33=\"%f\" v1=\"%f\" v2=\"%f\" v3=\"%f\"",
			m.R.m11, m.R.m12, m.R.m13, m.R.m21, m.R.m22, m.R.m23, m.R.m31, m.R.m32, m.R.m33, m.p.x, m.p.y, m.p.z);
	return std::string(buf);
}

std::string toXMLString(const grasp::ConfigMat34& cfg, const bool shortFormat = false)
{
	std::ostringstream os;
	os.precision(6);
	const size_t n = shortFormat ? 7 : cfg.c.size();
	for (size_t i = 0; i < n; ++i)
	{
		os << (i == 0 ? "c" : " c") << i + 1 << "=\"" << cfg.c[i] << "\"";
	}

	return os.str();
}

class ForceEvent
{
public:
	ForceEvent(grasp::FT* pFTSensor_, const golem::Twist& threshold_) :
		pFTSensor(pFTSensor_),
		threshold(threshold_),
		bias(Vec3::zero(), Vec3::zero())
	{
		if (pFTSensor == nullptr)
			throw Message(Message::LEVEL_CRIT, "ForceEvent(): pFTSensor is nullptr");
	}

	void setBias()
	{
		SecTmReal t;
		pFTSensor->readSensor(bias, t);
	}

	bool detected(golem::Context* pContext = nullptr)
	{
		bool tripped = false;
		golem::Twist current;
		SecTmReal t;
		pFTSensor->readSensor(current, t);
		double currentFT[6], biasFT[6], thresholdFT[6];
		current.get(currentFT);
		bias.get(biasFT);
		threshold.get(thresholdFT);
		for (size_t i = 0; i < 6; ++i)
		{
			if (abs(currentFT[i] - biasFT[i]) > thresholdFT[i])
			{
				tripped = true;
				if (pContext != nullptr)
					pContext->debug("Force event detected on axis %d: |%f - %f| > %f\n", i + 1, currentFT[i], biasFT[i], thresholdFT[i]);
				break;
			}
		}
		return tripped;
	}

public:
	golem::Twist threshold;
	golem::Twist bias;

private:
	grasp::FT* pFTSensor;

	ForceEvent();
};

}

//-----------------------------------------------------------------------------

const std::string Demo::ID_ANY = "Any";

const std::string Demo::Data::ModeName[MODE_LAST + 1] = {
	"Model",
	"Query",
	"Solution",
};

data::Data::Ptr Demo::Data::Desc::create(golem::Context &context) const {
	grasp::data::Data::Ptr data(new Demo::Data(context));
	static_cast<Demo::Data*>(data.get())->create(*this);
	return data;
}

Demo::Data::Data(golem::Context &context) : grasp::Player::Data(context), owner(nullptr) {
}

void Demo::Data::create(const Desc& desc) {
	Player::Data::create(desc);

	modelVertices.clear();
	modelTriangles.clear();
	modelFrame.setId();

	queryVertices.clear();
	queryTriangles.clear();
	queryFrame.setId();

	indexType = 0;
	indexItem = 0;
	contactRelation = grasp::Contact3D::RELATION_DFLT;

	indexDensity = 0;
	indexSolution = 0;

	mode = MODE_MODEL;
}

void Demo::Data::setOwner(Manager* owner) {
	grasp::Player::Data::setOwner(owner);
	this->owner = is<Demo>(owner);
	if (!this->owner)
		throw Message(Message::LEVEL_CRIT, "pacman::Demo::Data::setOwner(): unknown data owner");
	
	// initialise owner-dependent data members
	dataName = this->owner->dataName;
	modelState.reset(new golem::Controller::State(this->owner->controller->createState()));
	this->owner->controller->setToDefault(*modelState);
}

Demo::Data::Training::Map::iterator Demo::Data::getTrainingItem() {
	Training::Map::iterator ptr = training.begin();
	U32 indexType = 0;
	for (; ptr != training.end() && ptr == --training.end() && indexType < this->indexType; ++indexType, ptr = training.upper_bound(ptr->first));
	this->indexType = indexType;
	U32 indexItem = 0;
	for (; ptr != training.end() && ptr->first == (--training.end())->first && indexItem < this->indexItem; ++indexItem, ++ptr);
	this->indexItem = indexItem;
	return ptr;
}

void Demo::Data::setTrainingItem(Training::Map::const_iterator ptr) {
	U32 indexType = 0;
	for (Training::Map::const_iterator i = training.begin(); i != training.end(); ++indexType, i = training.upper_bound(i->first))
		if (i->first == ptr->first) {
			U32 indexItem = 0;
			for (Training::Map::const_iterator j = i; j != training.end(); ++indexItem, ++j)
				if (j == ptr) {
					this->indexType = indexType;
					this->indexItem = indexItem;
					return;
				}
		}
}

void Demo::Data::createRender() {
	Player::Data::createRender();
	{
		golem::CriticalSectionWrapper csw(owner->csRenderer);
		owner->modelRenderer.reset();
		// model/query
		const grasp::Vec3Seq& vertices = mode == MODE_MODEL ? modelVertices : queryVertices;
		const grasp::TriangleSeq& triangles = mode == MODE_MODEL ? modelTriangles : queryTriangles;
		const golem::Mat34& frame = mode == MODE_MODEL ? modelFrame : queryFrame;
		owner->modelRenderer.setColour(owner->modelColourSolid);
		owner->modelRenderer.addSolid(vertices.data(), (U32)vertices.size(), triangles.data(), (U32)triangles.size());
		owner->modelRenderer.setColour(owner->modelColourWire);
		owner->modelRenderer.addWire(vertices.data(), (U32)vertices.size(), triangles.data(), (U32)triangles.size());
		if (!vertices.empty() && !triangles.empty())
			owner->modelRenderer.addAxes3D(frame, Vec3(0.2));
		// training data
		if (mode == MODE_MODEL) {
			owner->contactAppearance.relation = contactRelation;
			Training::Map::iterator ptr = getTrainingItem();
			if (ptr != training.end()) {
				grasp::Contact3D::draw(owner->contactAppearance, ptr->second.contacts, modelFrame, owner->modelRenderer);
				for (auto &i : ptr->second.locations)
					owner->modelRenderer.addPoint(i, RGBA::BLACK);
			}
		}
		// query data
		else if (mode == MODE_QUERY) {
			if (!densities.empty()) {
				const Density::Seq::iterator ptr = densities.begin() + indexDensity;
				for (grasp::Query::Pose::Seq::const_iterator i = ptr->object.begin(); i != ptr->object.end(); ++i)
					owner->modelRenderer.addAxes(i->toMat34(), Vec3(0.005));
				for (grasp::Query::Pose::Seq::const_iterator i = ptr->pose.begin(); i != ptr->pose.end(); ++i)
					owner->modelRenderer.addAxes(i->toMat34(), Vec3(0.02));
			}
		}
		else if (mode == MODE_SOLUTION) {
			if (!solutions.empty()) {
				const Solution::Seq::iterator ptr = solutions.begin() + indexSolution;
				owner->modelRenderer.addAxes3D(ptr->pose.toMat34(), Vec3(0.1));
				if (ptr->path.size() > 0) owner->manipulatorAppearance.draw(*owner->manipulator, ptr->path[0], owner->modelRenderer);
				//if (ptr->path.size() > 1) owner->manipulatorAppearance.draw(*owner->manipulator, ptr->path[1], owner->modelRenderer);
				if (ptr->queryIndex < densities.size()) {
					// F = P * r
					Mat34 trn = ptr->path[0].toMat34() * owner->manipulator->getBaseFrame();
					for (auto &i : densities[ptr->queryIndex].locations) {
						Vec3 p;
						trn.multiply(p, i);
						owner->modelRenderer.addPoint(p, RGBA::BLACK);
					}
				}
			}
		}
	}
}

void Demo::Data::load(const std::string& prefix, const golem::XMLContext* xmlcontext, const data::Handler::Map& handlerMap) {
	data::Data::load(prefix, xmlcontext, handlerMap);

	try {
		dataName.clear();
		golem::XMLData("data_name", dataName, const_cast<golem::XMLContext*>(xmlcontext), false);
		if (dataName.length() > 0) {
			FileReadStream frs((prefix + sepName + dataName).c_str());
			
			modelVertices.clear();
			frs.read(modelVertices, modelVertices.end());
			modelTriangles.clear();
			frs.read(modelTriangles, modelTriangles.end());
			frs.read(modelFrame);
			frs.read(modelFrameOffset);

			queryVertices.clear();
			frs.read(queryVertices, queryVertices.end());
			queryTriangles.clear();
			frs.read(queryTriangles, queryTriangles.end());
			frs.read(queryFrame);

			modelState.reset(new golem::Controller::State(owner->controller->createState()));
			frs.read(*modelState);
			training.clear();
			frs.read(training, training.end(), std::make_pair(std::string(), Training(owner->controller->createState())));

			densities.clear();
			frs.read(densities, densities.end());
			solutions.clear();
			frs.read(solutions, solutions.end());
		}
	}
	catch (const std::exception&) {
	}
}

void Demo::Data::save(const std::string& prefix, golem::XMLContext* xmlcontext) const {
	data::Data::save(prefix, xmlcontext);

	if (dataName.length() > 0) {
		golem::XMLData("data_name", const_cast<std::string&>(dataName), xmlcontext, true);
		FileWriteStream fws((prefix + sepName + dataName).c_str());

		fws.write(modelVertices.begin(), modelVertices.end());
		fws.write(modelTriangles.begin(), modelTriangles.end());
		fws.write(modelFrame);
		fws.write(modelFrameOffset);

		fws.write(queryVertices.begin(), queryVertices.end());
		fws.write(queryTriangles.begin(), queryTriangles.end());
		fws.write(queryFrame);

		fws.write(*modelState);
		fws.write(training.begin(),training.end());

		fws.write(densities.begin(), densities.end());
		fws.write(solutions.begin(), solutions.end());
	}
}

//------------------------------------------------------------------------------

void Demo::PoseDensity::load(const golem::XMLContext* xmlcontext) {
	grasp::XMLData(stdDev, xmlcontext->getContextFirst("std_dev"));
	stdDev.ang = Math::sqrt(REAL_ONE / stdDev.ang);	// stdDev ~ 1/cov

	golem::XMLData("kernels", kernels, const_cast<golem::XMLContext*>(xmlcontext));
	grasp::XMLData(pathDist, xmlcontext->getContextFirst("path_dist"));
	pathDist.ang = Math::sqrt(REAL_ONE / pathDist.ang);	// stdDev ~ 1/cov

	golem::XMLData("std_dev", pathDistStdDev, xmlcontext->getContextFirst("path_dist"));
}

//------------------------------------------------------------------------------

void Demo::Optimisation::load(const golem::XMLContext* xmlcontext) {
	golem::XMLData("runs", runs, const_cast<golem::XMLContext*>(xmlcontext));
	golem::XMLData("steps", steps, const_cast<golem::XMLContext*>(xmlcontext));
	golem::XMLData("sa_temp", saTemp, const_cast<golem::XMLContext*>(xmlcontext));
	golem::XMLData("sa_delta_lin", saDelta.lin, const_cast<golem::XMLContext*>(xmlcontext));
	golem::XMLData("sa_delta_ang", saDelta.ang, const_cast<golem::XMLContext*>(xmlcontext));
	golem::XMLData("sa_energy", saEnergy, const_cast<golem::XMLContext*>(xmlcontext));
}

//------------------------------------------------------------------------------

void Demo::Desc::load(golem::Context& context, const golem::XMLContext* xmlcontext) {
	Player::Desc::load(context, xmlcontext);

	xmlcontext = xmlcontext->getContextFirst("demo");

	golem::XMLData("data_name", dataName, const_cast<golem::XMLContext*>(xmlcontext));

	golem::XMLData("camera", modelCamera, xmlcontext->getContextFirst("model"));
	golem::XMLData("handler", modelHandler, xmlcontext->getContextFirst("model"));
	golem::XMLData("item", modelItem, xmlcontext->getContextFirst("model"));
	golem::XMLData("item_obj", modelItemObj, xmlcontext->getContextFirst("model"));

	modelScanPose.xmlData(xmlcontext->getContextFirst("model scan_pose"));
	golem::XMLData(modelColourSolid, xmlcontext->getContextFirst("model colour solid"));
	golem::XMLData(modelColourWire, xmlcontext->getContextFirst("model colour wire"));

	golem::XMLData("handler_trj", modelHandlerTrj, xmlcontext->getContextFirst("model"));
	golem::XMLData("item_trj", modelItemTrj, xmlcontext->getContextFirst("model"));

	golem::XMLData("camera", queryCamera, xmlcontext->getContextFirst("query"));
	golem::XMLData("handler", queryHandler, xmlcontext->getContextFirst("query"));
	golem::XMLData("item", queryItem, xmlcontext->getContextFirst("query"));
	golem::XMLData("item_obj", queryItemObj, xmlcontext->getContextFirst("query"));

	golem::XMLData("handler_trj", queryHandlerTrj, xmlcontext->getContextFirst("query"));
	golem::XMLData("item_trj", queryItemTrj, xmlcontext->getContextFirst("query"));

	golem::XMLData("sensor", graspSensorForce, xmlcontext->getContextFirst("grasp"));
	golem::XMLData(graspThresholdForce, xmlcontext->getContextFirst("grasp threshold"));
	golem::XMLData("event_time_wait", graspEventTimeWait, xmlcontext->getContextFirst("grasp"));
	golem::XMLData("close_duration", graspCloseDuration, xmlcontext->getContextFirst("grasp"));
	graspPoseOpen.xmlData(xmlcontext->getContextFirst("grasp pose_open"));
	graspPoseClosed.xmlData(xmlcontext->getContextFirst("grasp pose_closed"));

	golem::XMLData("camera", objectCamera, xmlcontext->getContextFirst("object"));
	golem::XMLData("handler_scan", objectHandlerScan, xmlcontext->getContextFirst("object"));
	golem::XMLData("handler", objectHandler, xmlcontext->getContextFirst("object"));
	golem::XMLData("item_scan", objectItemScan, xmlcontext->getContextFirst("object"));
	golem::XMLData("item", objectItem, xmlcontext->getContextFirst("object"));
	objectScanPoseSeq.clear();
	XMLData(objectScanPoseSeq, objectScanPoseSeq.max_size(), xmlcontext->getContextFirst("object"), "scan_pose");
	objectFrameAdjustment.load(xmlcontext->getContextFirst("object frame_adjustment"));

	modelDescMap.clear();
	golem::XMLData(modelDescMap, modelDescMap.max_size(), xmlcontext->getContextFirst("model"), "model", false);
	contactAppearance.load(xmlcontext->getContextFirst("model appearance"));

	queryDescMap.clear();
	golem::XMLData(queryDescMap, queryDescMap.max_size(), xmlcontext->getContextFirst("query"), "query", false);
	poseMap.clear();
	golem::XMLData(poseMap, poseMap.max_size(), xmlcontext->getContextFirst("query"), "query", false);

	optimisation.load(xmlcontext->getContextFirst("query optimisation"));

	manipulatorDesc->load(xmlcontext->getContextFirst("manipulator"));
	manipulatorAppearance.load(xmlcontext->getContextFirst("manipulator appearance"));
	golem::XMLData("item_trj", manipulatorItemTrj, xmlcontext->getContextFirst("manipulator"));
	grasp::XMLData(manipulatorPoseStdDev, xmlcontext->getContextFirst("manipulator pose_stddev"), false);

	golem::XMLData("duration", manipulatorTrajectoryDuration, xmlcontext->getContextFirst("manipulator trajectory"));
	golem::XMLData(trajectoryThresholdForce, xmlcontext->getContextFirst("manipulator threshold"));

	golem::XMLData("release_fraction", withdrawReleaseFraction, xmlcontext->getContextFirst("manipulator withdraw_action"));
	golem::XMLData("lift_distance", withdrawLiftDistance, xmlcontext->getContextFirst("manipulator withdraw_action"));
}

//------------------------------------------------------------------------------

pacman::Demo::Demo(Scene &scene) : 
	Player(scene),
	modelCamera(nullptr), queryCamera(nullptr), modelHandler(nullptr), modelHandlerTrj(nullptr), queryHandler(nullptr), queryHandlerTrj(nullptr), graspSensorForce(nullptr), objectCamera(nullptr), objectHandlerScan(nullptr), objectHandler(nullptr)
{}

pacman::Demo::~Demo() {
}

//------------------------------------------------------------------------------

grasp::Camera* Demo::getWristCamera(const bool dontThrow) const
{
	const std::string id("OpenNI+OpenNI");
	grasp::Sensor::Map::const_iterator i = sensorMap.find(id);
	if (i == sensorMap.end())
	{
		if (dontThrow) return nullptr;
		context.write("%s was not found\n", id.c_str());
		throw Cancel("getWristCamera: wrist-mounted camera is not available");
	}

	grasp::Camera* camera = grasp::is<grasp::Camera>(i);

	// want the wrist-mounted camera
	if (!camera->hasVariableMounting())
	{
		if (dontThrow) return nullptr;
		context.write("%s is a static camera\n", id.c_str());
		throw Cancel("getWristCamera: wrist-mounted camera is not available");
	}

	return camera;
}

golem::Mat34 Demo::getWristPose() const
{
	const golem::U32 wristJoint = 6; // @@@
	grasp::ConfigMat34 pose;
	getPose(wristJoint, pose);
	return pose.w;
}

golem::Controller::State::Seq Demo::getTrajectoryFromPose(const golem::Mat34& w, const SecTmReal duration)
{
	const golem::Mat34 R = controller->getChains()[armInfo.getChains().begin()]->getReferencePose();
	const golem::Mat34 wR = w * R;

	golem::Controller::State begin = controller->createState();
	controller->lookupState(SEC_TM_REAL_MAX, begin);

	golem::Controller::State::Seq trajectory;
	const grasp::RBDist err = findTrajectory(begin, nullptr, &wR, duration, trajectory);

	return trajectory;
}

grasp::ConfigMat34 Demo::getConfigFromPose(const golem::Mat34& w)
{
	golem::Controller::State::Seq trajectory = getTrajectoryFromPose(w, SEC_TM_REAL_ZERO);
	const golem::Controller::State& last = trajectory.back();
	ConfigMat34 cfg(RealSeq(61,0.0)); // !!! TODO use proper indices
	for (size_t i = 0; i < 7; ++i)
	{
		cfg.c[i] = last.cpos.data()[i];
	}
	return cfg;
}

void Demo::setHandConfig(Controller::State::Seq& trajectory, const grasp::ConfigMat34& handPose)
{
	ConfigspaceCoord cposHand;
	cposHand.set(handPose.c.data(), handPose.c.data() + std::min(handPose.c.size(), (size_t)info.getJoints().size()));

	for (Controller::State::Seq::iterator i = trajectory.begin(); i != trajectory.end(); ++i)
	{
		Controller::State& state = *i;
		state.setToDefault(handInfo.getJoints().begin(), handInfo.getJoints().end());
		state.cpos.set(handInfo.getJoints(), cposHand);
	}
}

void Demo::gotoWristPose(const golem::Mat34& w, const SecTmReal duration)
{
	golem::Controller::State::Seq trajectory = getTrajectoryFromPose(w, duration);
	sendTrajectory(trajectory);
	controller->waitForEnd();
	Sleep::msleep(SecToMSec(trajectoryIdleEnd));
}

void Demo::gotoPose2(const ConfigMat34& pose, const SecTmReal duration)
{
	golem::Controller::State begin = lookupState();	// current state
	golem::Controller::State end = begin;
	end.cpos.set(pose.c.data(), pose.c.data() + std::min(pose.c.size(), (size_t)info.getJoints().size()));
	golem::Controller::State::Seq trajectory;
	findTrajectory(begin, &end, nullptr, duration, trajectory);
	sendTrajectory(trajectory);
	controller->waitForEnd();
	Sleep::msleep(SecToMSec(trajectoryIdleEnd));
}

void Demo::releaseHand(const double openFraction, const SecTmReal duration)
{
	double f = 1.0 - openFraction;
	f = std::max(0.0, std::min(1.0, f));

	golem::Controller::State currentState = lookupState();
	ConfigMat34 openPose(RealSeq(61, 0.0)); // !!! TODO use proper indices
	for (size_t i = 0; i < openPose.c.size(); ++i)
		openPose.c[i] = currentState.cpos.data()[i];

	// TODO use proper indices - handInfo.getJoints()
	const size_t handIndexBegin = 7;
	const size_t handIndexEnd   = handIndexBegin + 5*4;
	for (size_t i = handIndexBegin; i < handIndexEnd; ++i)
		openPose.c[i] *= f;

	gotoPose2(openPose, duration);
}

void Demo::liftWrist(const double verticalDistance, const SecTmReal duration)
{
	// vertically by verticalDistance; to hand zero config
	Mat34 pose = getWristPose();
	pose.p.z += std::max(0.0, verticalDistance);
	gotoWristPose(pose, duration);

	// TODO open hand while lifting
}

void Demo::haltRobot()
{
	context.debug("STOPPING ROBOT!");

	Controller::State::Seq trj;

	Controller::State command = controller->createState();
	const Real t = controller->getCommandTime();
	controller->lookupCommand(t, command);
	command.cvel.setToDefault(info.getJoints());
	command.cacc.setToDefault(info.getJoints());
	command.t = t;
	trj.push_back(command);

	Controller::State command2 = command;
	command2.t += controller->getCycleDuration();
	trj.push_back(command2);

	controller->send(&trj.front(), &trj.back() + 1, true, false);
}

//------------------------------------------------------------------------------

void Demo::nudgeWrist()
{
	auto showPose = [&](const std::string& description, const golem::Mat34& m) {
		context.write("%s: p={(%f, %f, %f)}, R={(%f, %f, %f), (%f, %f, %f), (%f, %f, %f)}\n", description.c_str(), m.p.x, m.p.y, m.p.z, m.R.m11, m.R.m12, m.R.m13, m.R.m21, m.R.m22, m.R.m23, m.R.m31, m.R.m32, m.R.m33);
	};

	double s = 5; // 5cm step
	int dirCode;
	for (;;)
	{
		std::ostringstream os;
		os << "up:7 down:1 left:4 right:6 back:8 front:2 null:0 STEP(" << s << " cm):+/-";
		dirCode = option("7146820+-", os.str().c_str());
		if (dirCode == '+')
		{
			s *= 2;
			if (s > 20.0) s = 20.0;
			continue;
		}
		if (dirCode == '-')
		{
			s /= 2;
			continue;
		}
		break;
	}
	s /= 100.0; // cm -> m

	golem::Vec3 nudge(golem::Vec3::zero());
	switch (dirCode)
	{
	case '7': // up
		nudge.z = s;
		break;
	case '1': // down
		nudge.z = -s;
		break;
	case '4': // left
		nudge.y = -s;
		break;
	case '6': // right
		nudge.y = s;
		break;
	case '8': // back
		nudge.x = -s;
		break;
	case '2': // front
		nudge.x = s;
		break;
	};

	golem::Mat34 pose;

	grasp::Camera* camera = getWristCamera(true);
	if (camera != nullptr)
	{
		pose = camera->getFrame();
		showPose("camera before", pose);
	}

	pose = getWristPose();
	showPose("before", pose);

	pose.p = pose.p + nudge;
	showPose("commanded", pose);

	gotoWristPose(pose);

	showPose("final wrist", getWristPose());
	if (camera != nullptr)
	{
		pose = camera->getFrame();
		showPose("final camera", pose);
	}

	grasp::ConfigMat34 cp;
	getPose(0, cp);
	context.debug("%s\n", toXMLString(cp, true).c_str());
}

void Demo::rotateObjectInHand()
{
	auto showPose = [&](const std::string& description, const golem::Mat34& m) {
		context.write("%s: p={(%f, %f, %f)}, R={(%f, %f, %f), (%f, %f, %f), (%f, %f, %f)}\n", description.c_str(), m.p.x, m.p.y, m.p.z, m.R.m11, m.R.m12, m.R.m13, m.R.m21, m.R.m22, m.R.m23, m.R.m31, m.R.m32, m.R.m33);
	};

	const double maxstep = 45.0;
	double angstep = 30.0;
	int rotType;
	for (;;)
	{
		std::ostringstream os;
		os << "rotation: Clockwise or Anticlockwise screw, Up or Down, Left or Right; STEP(" << angstep << " deg):+/-";
		rotType = option("CAUDLR+-", os.str().c_str());
		if (rotType == '+')
		{
			angstep *= 2;
			if (angstep > maxstep) angstep = maxstep;
			continue;
		}
		if (rotType == '-')
		{
			angstep /= 2;
			continue;
		}
		break;
	}

	golem::Vec3 rotAxis(golem::Vec3::zero());

	switch (rotType)
	{
	case 'C':
		rotAxis.z = 1.0;
		break;
	case 'A':
		rotAxis.z = 1.0;
		angstep = -angstep;
		break;
	case 'U':
		rotAxis.y = 1.0;
		break;
	case 'D':
		rotAxis.y = 1.0;
		angstep = -angstep;
		break;
	case 'L':
		rotAxis.x = 1.0;
		break;
	case 'R':
		rotAxis.x = 1.0;
		angstep = -angstep;
		break;
	}

	golem::Mat33 rot(golem::Mat33(angstep / 180.0 * golem::REAL_PI, rotAxis));

	golem::Mat34 pose = getWristPose();
	showPose("before", pose);

	pose.R = pose.R * rot;
	showPose("commanded", pose);

	gotoWristPose(pose);
	showPose("final wrist", getWristPose());

	grasp::ConfigMat34 cp;
	getPose(0, cp);
	context.debug("%s\n", toXMLString(cp, true).c_str());

	//recordingStart(dataCurrentPtr->first, recordingLabel, false);
	//context.write("taken snapshot\n");
}

//------------------------------------------------------------------------------

void pacman::Demo::create(const Desc& desc) {
	desc.assertValid(Assert::Context("pacman::Demo::Desc."));

	// create object
	Player::create(desc); // throws

	dataName = desc.dataName;

	grasp::Sensor::Map::const_iterator modelCameraPtr = sensorMap.find(desc.modelCamera);
	modelCamera = modelCameraPtr != sensorMap.end() ? is<Camera>(modelCameraPtr->second.get()) : nullptr;
	if (!modelCamera)
		throw Message(Message::LEVEL_CRIT, "pacman::Demo::create(): unknown model pose estimation camera: %s", desc.modelCamera.c_str());
	grasp::data::Handler::Map::const_iterator modelHandlerPtr = handlerMap.find(desc.modelHandler);
	modelHandler = modelHandlerPtr != handlerMap.end() ? modelHandlerPtr->second.get() : nullptr;
	if (!modelHandler)
		throw Message(Message::LEVEL_CRIT, "pacman::Demo::create(): unknown model data handler: %s", desc.modelHandler.c_str());
	modelItem = desc.modelItem;
	modelItemObj = desc.modelItemObj;

	modelScanPose = desc.modelScanPose;
	modelColourSolid = desc.modelColourSolid;
	modelColourWire = desc.modelColourWire;

	grasp::data::Handler::Map::const_iterator modelHandlerTrjPtr = handlerMap.find(desc.modelHandlerTrj);
	modelHandlerTrj = modelHandlerTrjPtr != handlerMap.end() ? modelHandlerTrjPtr->second.get() : nullptr;
	if (!modelHandlerTrj)
		throw Message(Message::LEVEL_CRIT, "pacman::Demo::create(): unknown model trajectory handler: %s", desc.modelHandlerTrj.c_str());
	modelItemTrj = desc.modelItemTrj;

	grasp::Sensor::Map::const_iterator queryCameraPtr = sensorMap.find(desc.queryCamera);
	queryCamera = queryCameraPtr != sensorMap.end() ? is<Camera>(queryCameraPtr->second.get()) : nullptr;
	if (!queryCamera)
		throw Message(Message::LEVEL_CRIT, "pacman::Demo::create(): unknown query pose estimation camera: %s", desc.queryCamera.c_str());
	grasp::data::Handler::Map::const_iterator queryHandlerPtr = handlerMap.find(desc.queryHandler);
	queryHandler = queryHandlerPtr != handlerMap.end() ? queryHandlerPtr->second.get() : nullptr;
	if (!queryHandler)
		throw Message(Message::LEVEL_CRIT, "pacman::Demo::create(): unknown query data handler: %s", desc.queryHandler.c_str());
	queryItem = desc.queryItem;
	queryItemObj = desc.queryItemObj;

	grasp::data::Handler::Map::const_iterator queryHandlerTrjPtr = handlerMap.find(desc.queryHandlerTrj);
	queryHandlerTrj = queryHandlerTrjPtr != handlerMap.end() ? queryHandlerTrjPtr->second.get() : nullptr;
	if (!queryHandlerTrj)
		throw Message(Message::LEVEL_CRIT, "pacman::Demo::create(): unknown query trajectory handler: %s", desc.queryHandlerTrj.c_str());
	queryItemTrj = desc.queryItemTrj;

	grasp::Sensor::Map::const_iterator graspSensorForcePtr = sensorMap.find(desc.graspSensorForce);
	graspSensorForce = graspSensorForcePtr != sensorMap.end() ? is<FT>(graspSensorForcePtr->second.get()) : nullptr;
	if (!graspSensorForce)
		throw Message(Message::LEVEL_CRIT, "pacman::Demo::create(): unknown grasp F/T sensor: %s", desc.graspSensorForce.c_str());
	graspThresholdForce = desc.graspThresholdForce;
	graspEventTimeWait = desc.graspEventTimeWait;
	graspCloseDuration = desc.graspCloseDuration;
	graspPoseOpen = desc.graspPoseOpen;
	graspPoseClosed = desc.graspPoseClosed;

	grasp::Sensor::Map::const_iterator objectCameraPtr = sensorMap.find(desc.objectCamera);
	objectCamera = objectCameraPtr != sensorMap.end() ? is<Camera>(objectCameraPtr->second.get()) : nullptr;
	if (!objectCamera)
		throw Message(Message::LEVEL_CRIT, "pacman::Demo::create(): unknown object capture camera: %s", desc.objectCamera.c_str());
	grasp::data::Handler::Map::const_iterator objectHandlerScanPtr = handlerMap.find(desc.objectHandlerScan);
	objectHandlerScan = objectHandlerScanPtr != handlerMap.end() ? objectHandlerScanPtr->second.get() : nullptr;
	if (!objectHandlerScan)
		throw Message(Message::LEVEL_CRIT, "pacman::Demo::create(): unknown object (scan) data handler: %s", desc.objectHandlerScan.c_str());
	grasp::data::Handler::Map::const_iterator objectHandlerPtr = handlerMap.find(desc.objectHandler);
	objectHandler = objectHandlerPtr != handlerMap.end() ? objectHandlerPtr->second.get() : nullptr;
	if (!objectHandler)
		throw Message(Message::LEVEL_CRIT, "pacman::Demo::create(): unknown object (process) data handler: %s", desc.objectHandler.c_str());
	objectItemScan = desc.objectItemScan;
	objectItem = desc.objectItem;
	objectScanPoseSeq = desc.objectScanPoseSeq;
	objectFrameAdjustment = desc.objectFrameAdjustment;

	// models
	modelMap.clear();
	for (Model::Desc::Map::const_iterator i = desc.modelDescMap.begin(); i != desc.modelDescMap.end(); ++i)
		modelMap.insert(std::make_pair(i->first, i->second->create(context, i->first)));
	contactAppearance = desc.contactAppearance;

	// query densities
	queryMap.clear();
	for (Query::Desc::Map::const_iterator i = desc.queryDescMap.begin(); i != desc.queryDescMap.end(); ++i)
		queryMap.insert(std::make_pair(i->first, i->second->create(context, i->first)));
	poseMap = desc.poseMap;

	optimisation = desc.optimisation;

	// manipulator
	manipulator = desc.manipulatorDesc->create(*planner, desc.controllerIDSeq);
	manipulatorAppearance = desc.manipulatorAppearance;
	manipulatorItemTrj = desc.manipulatorItemTrj;

	poseCovInv.lin = REAL_ONE / (poseCov.lin = Math::sqr(desc.manipulatorPoseStdDev.lin));
	poseCovInv.ang = REAL_ONE / (poseCov.ang = Math::sqr(desc.manipulatorPoseStdDev.ang));
	poseDistanceMax = Math::sqr(desc.manipulatorPoseStdDevMax);

	manipulatorTrajectoryDuration = desc.manipulatorTrajectoryDuration;
	trajectoryThresholdForce = desc.trajectoryThresholdForce;

	withdrawReleaseFraction = desc.withdrawReleaseFraction;
	withdrawLiftDistance = desc.withdrawLiftDistance;


	////////////////////////////////////////////////////////////////////////////
	//                            START OF MENUS                              // 
	////////////////////////////////////////////////////////////////////////////


	// top menu help using global key '?'
	scene.getHelp().insert(Scene::StrMapVal("0F5", "  P                                       menu PaCMan\n"));
	scene.getHelp().insert(Scene::StrMapVal("0F5", "  Z                                       menu SZ Tests\n"));
	scene.getHelp().insert(Scene::StrMapVal("0F5", "  <Tab>                                   Query/Model mode switch\n"));
	scene.getHelp().insert(Scene::StrMapVal("0F5", "  ()                                      Training item selection\n"));

	menuCmdMap.insert(std::make_pair("\t", [=]() {
		// set mode
		to<Data>(dataCurrentPtr)->mode = Data::Mode(to<Data>(dataCurrentPtr)->mode >= Data::MODE_LAST ? (U32)Data::MODE_FIRST : U32(to<Data>(dataCurrentPtr)->mode) + 1);
		context.write("%s mode\n", Data::ModeName[to<Data>(dataCurrentPtr)->mode].c_str());
		createRender();
	}));
	itemSelect = [&](ItemSelectFunc itemSelectFunc) {
		if (to<Data>(dataCurrentPtr)->training.empty())
			throw Cancel("No training data");
		Data::Training::Map::iterator ptr = to<Data>(dataCurrentPtr)->getTrainingItem();
		itemSelectFunc(to<Data>(dataCurrentPtr)->training, ptr);
		to<Data>(dataCurrentPtr)->setTrainingItem(ptr);
		context.write("Model type %s/%u, Item #%u\n", ptr->first.c_str(), to<Data>(dataCurrentPtr)->indexType + 1, to<Data>(dataCurrentPtr)->indexItem + 1);
	};
	menuCmdMap.insert(std::make_pair("(", [&]() {
		if (to<Data>(dataCurrentPtr)->mode == Data::MODE_MODEL)
			itemSelect([] (Data::Training::Map& map, Data::Training::Map::iterator& ptr) {
				if (ptr == map.begin()) ptr = --map.end(); else --ptr;
			});
		else if (to<Data>(dataCurrentPtr)->mode == Data::MODE_QUERY) {
			if (to<Data>(dataCurrentPtr)->densities.empty())
				throw Cancel("No query densities created");
			to<Data>(dataCurrentPtr)->indexDensity = to<Data>(dataCurrentPtr)->indexDensity <= 0 ? to<Data>(dataCurrentPtr)->densities.size() - 1 : to<Data>(dataCurrentPtr)->indexDensity - 1;
			context.write("Query type %s, Item #%u\n", to<Data>(dataCurrentPtr)->densities[to<Data>(dataCurrentPtr)->indexDensity].type.c_str(), to<Data>(dataCurrentPtr)->indexDensity + 1);
		}
		else if (to<Data>(dataCurrentPtr)->mode == Data::MODE_SOLUTION) {
			if (to<Data>(dataCurrentPtr)->solutions.empty())
				throw Cancel("No solutions created");
			to<Data>(dataCurrentPtr)->indexSolution = to<Data>(dataCurrentPtr)->indexSolution <= 0 ? to<Data>(dataCurrentPtr)->solutions.size() - 1 : to<Data>(dataCurrentPtr)->indexSolution - 1;
			const Data::Solution& solution = to<Data>(dataCurrentPtr)->solutions[to<Data>(dataCurrentPtr)->indexSolution];
			context.write("Solution type %s (%u/%u), Item #%u, Likelihood_{total, contact, pose, collision}={%.6e, %.6e, %.6e, %.6e}\n", solution.type.c_str(), solution.queryIndex + 1, to<Data>(dataCurrentPtr)->densities.size(), to<Data>(dataCurrentPtr)->indexSolution + 1, solution.likelihood.likelihood, solution.likelihood.contact, solution.likelihood.pose, solution.likelihood.collision);
		}
		createRender();
	}));
	menuCmdMap.insert(std::make_pair(")", [&]() {
		if (to<Data>(dataCurrentPtr)->mode == Data::MODE_MODEL)
			itemSelect([](Data::Training::Map& map, Data::Training::Map::iterator& ptr) {
				if (ptr == --map.end()) ptr = map.begin(); else ++ptr;
			});
		else if (to<Data>(dataCurrentPtr)->mode == Data::MODE_QUERY) {
			if (to<Data>(dataCurrentPtr)->densities.empty())
				throw Cancel("No query densities created");
			to<Data>(dataCurrentPtr)->indexDensity = to<Data>(dataCurrentPtr)->indexDensity < to<Data>(dataCurrentPtr)->densities.size() - 1 ? to<Data>(dataCurrentPtr)->indexDensity + 1 : 0;
			context.write("Query type %s, Item #%u\n", to<Data>(dataCurrentPtr)->densities[to<Data>(dataCurrentPtr)->indexDensity].type.c_str(), to<Data>(dataCurrentPtr)->indexDensity + 1);
		}
		else if (to<Data>(dataCurrentPtr)->mode == Data::MODE_SOLUTION) {
			if (to<Data>(dataCurrentPtr)->solutions.empty())
				throw Cancel("No solutions created");
			to<Data>(dataCurrentPtr)->indexSolution = to<Data>(dataCurrentPtr)->indexSolution < to<Data>(dataCurrentPtr)->solutions.size() - 1 ? to<Data>(dataCurrentPtr)->indexSolution + 1 : 0;
			const Data::Solution& solution = to<Data>(dataCurrentPtr)->solutions[to<Data>(dataCurrentPtr)->indexSolution];
			context.write("Solution type %s (%u/%u), Item #%u, Likelihood_{total, contact, pose, collision}={%.6e, %.6e, %.6e, %.6e}\n", solution.type.c_str(), solution.queryIndex + 1, to<Data>(dataCurrentPtr)->densities.size(), to<Data>(dataCurrentPtr)->indexSolution + 1, solution.likelihood.likelihood, solution.likelihood.contact, solution.likelihood.pose, solution.likelihood.collision);
		}
		createRender();
	}));

	// data menu control and commands
	menuCtrlMap.insert(std::make_pair("P", [=](MenuCmdMap& menuCmdMap, std::string& desc) {
		desc = "Press a key to: (E)stimate pose, (C)apture object, (M)odel, (Q)uery, (T)rajectory, run (D)emo ...";
	}));

	// model pose estimation
	menuCtrlMap.insert(std::make_pair("PE", [=](MenuCmdMap& menuCmdMap, std::string& desc) {
		desc = "Press a key to: (M)odel/(Q)ery estimation...";
	}));
	menuCmdMap.insert(std::make_pair("PEM", [=]() {
		// estimate
		(void)estimatePose(Data::MODE_MODEL);
		// finish
		context.write("Done!\n");
	}));
	menuCmdMap.insert(std::make_pair("PEQ", [=]() {
		// estimate
		(void)estimatePose(Data::MODE_QUERY);
		// finish
		context.write("Done!\n");
	}));

	// model attachement
	menuCtrlMap.insert(std::make_pair("PC", [=](MenuCmdMap& menuCmdMap, std::string& desc) {
		desc = "Press a key to: (C)amera/(L)oad...";
	}));
	menuCmdMap.insert(std::make_pair("PCC", [=]() {
		// grasp and scan object
		grasp::data::Item::Map::iterator ptr = objectGraspAndCapture();
		// compute features and add to data bundle
		(void)objectProcess(ptr);
		// finish
		context.write("Done!\n");
	}));
	menuCmdMap.insert(std::make_pair("PCL", [=]() {
		// cast to data::Import
		data::Import* import = is<data::Import>(objectHandlerScan);
		if (!import)
			throw Cancel("Object handler does not implement data::Import");

		// replace current handlers
		UI::removeCallback(*this, getCurrentHandler());
		UI::addCallback(*this, import);

		// load/import item
		readPath("Enter file path: ", dataImportPath, import->getFileTypes());
		data::Item::Ptr item = import->import(dataImportPath);

		ScopeGuard guard([&]() { golem::CriticalSectionWrapper csw(csRenderer); objectRenderer.reset(); });
		RenderBlock renderBlock(*this);

		// compute reference frame and adjust object frame
		data::Location3D* location = is<data::Location3D>(item.get());
		if (!location)
			throw Cancel("Object handler does not implement data::Location3D");
		Vec3Seq points, pointsTrn;
		for (size_t i = 0; i < location->getNumOfLocations(); ++i)
			points.push_back(location->getLocation(i));
		pointsTrn.resize(points.size());
		Mat34 frame = RBPose::createFrame(points), frameInv;
		frameInv.setInverse(frame);
		context.write("Press a key to: (%s/%s) to adjust position/orientation, (%s) to adjust increment, finish <Enter>...\n",
			objectFrameAdjustment.linKeys.c_str(), objectFrameAdjustment.angKeys.c_str(), objectFrameAdjustment.incKeys.c_str());
		for (bool finish = false; !finish;) {
			const Mat34 trn = frame * frameInv; // frame = trn * frameInit, trn = frame * frameInit^-1
			for (size_t i = 0; i < points.size(); ++i)
				trn.multiply(pointsTrn[i], points[i]);
			{
				golem::CriticalSectionWrapper csw(csRenderer);
				objectRenderer.reset();
				for (size_t i = 0; i < pointsTrn.size(); ++i)
					objectRenderer.addPoint(pointsTrn[i], objectFrameAdjustment.colourSolid);
				objectRenderer.addAxes3D(frame, objectFrameAdjustment.frameSize);
			}
			const int key = waitKey();
			switch (key) {
			case 27: throw Cancel("Cancelled");
			case 13: finish = true; break;
			default:
				if (objectFrameAdjustment.adjustIncrement(key))
					context.write("Inrement: position = %f [m], orientation = %f [deg]\n", objectFrameAdjustment.getIncrement().lin, Math::radToDeg(objectFrameAdjustment.getIncrement().ang));
				(void)objectFrameAdjustment.adjustFrame(key, frame);
			}
		}
		// transform
		location->transform(frame * frameInv);

		// add item to data bundle
		data::Item::Map::iterator ptr;
		{
			golem::CriticalSectionWrapper cswData(csData);
			to<Data>(dataCurrentPtr)->itemMap.erase(objectItemScan);
			ptr = to<Data>(dataCurrentPtr)->itemMap.insert(to<Data>(dataCurrentPtr)->itemMap.end(), data::Item::Map::value_type(objectItemScan, item));
			Data::View::setItem(to<Data>(dataCurrentPtr)->itemMap, ptr, to<Data>(dataCurrentPtr)->getView());
		}

		// compute features and add to data bundle
		(void)objectProcess(ptr);

		// finish
		context.write("Done!\n");
	}));

	// model operations
	menuCtrlMap.insert(std::make_pair("PM", [=](MenuCmdMap& menuCmdMap, std::string& desc) {
		// set mode
		to<Data>(dataCurrentPtr)->mode = Data::MODE_MODEL;
		createRender();

		desc = "Press a key to: (S)et/(G)oto initial object model pose, (A)dd/(R)emove training data...";
	}));
	menuCmdMap.insert(std::make_pair("PMS", [=]() {
		// load object item
		data::Item::Map::iterator ptr = to<Data>(dataCurrentPtr)->itemMap.find(objectItem);
		if (ptr == to<Data>(dataCurrentPtr)->itemMap.end())
			throw Cancel("Object item has not been created");
		// clone object item
		data::Item::Ptr item = ptr->second->clone();
		// insert as model object item
		{
			RenderBlock renderBlock(*this);
			golem::CriticalSectionWrapper cswData(csData);
			to<Data>(dataCurrentPtr)->itemMap.erase(modelItemObj);
			ptr = to<Data>(dataCurrentPtr)->itemMap.insert(to<Data>(dataCurrentPtr)->itemMap.end(), data::Item::Map::value_type(modelItemObj, item));
			Data::View::setItem(to<Data>(dataCurrentPtr)->itemMap, ptr, to<Data>(dataCurrentPtr)->getView());
		}
		// set model robot pose
		to<Data>(dataCurrentPtr)->modelState.reset(new golem::Controller::State(lookupState()));

		context.write("Done!\n");
	}));
	menuCmdMap.insert(std::make_pair("PMG", [=]() {
		// load model object item
		{
			RenderBlock renderBlock(*this);
			golem::CriticalSectionWrapper cswData(csData);
			data::Item::Map::iterator ptr = to<Data>(dataCurrentPtr)->itemMap.find(modelItemObj);
			if (ptr == to<Data>(dataCurrentPtr)->itemMap.end())
				throw Message(Message::LEVEL_ERROR, "Model object item has not been created");
			Data::View::setItem(to<Data>(dataCurrentPtr)->itemMap, ptr, to<Data>(dataCurrentPtr)->getView());
		}
		// go to model robot pose
		gotoConfig(*to<Data>(dataCurrentPtr)->modelState);

		context.write("Done!\n");
	}));

	menuCmdMap.insert(std::make_pair("PMA", [=]() {
		// load model object item
		const data::Item::Map::iterator ptr = to<Data>(dataCurrentPtr)->itemMap.find(modelItemObj);
		if (ptr == to<Data>(dataCurrentPtr)->itemMap.end())
			throw Message(Message::LEVEL_ERROR, "Model object item has not been created");

		// select model any
		const grasp::Model::Map::const_iterator modelAny = modelMap.find(ID_ANY);
		if (modelAny == modelMap.end())
			throw Message(Message::LEVEL_ERROR, "Unable to find Model %s", ID_ANY.c_str());

		// clear
		ScopeGuard guard([&]() { golem::CriticalSectionWrapper csw(csRenderer); objectRenderer.reset(); });
		RenderBlock renderBlock(*this);

		// select model type
		std::string modelType;
		try {
			grasp::StringSeq modelTypeSeq;
			for (Data::Training::Map::const_iterator i = to<Data>(dataCurrentPtr)->training.begin(); i != to<Data>(dataCurrentPtr)->training.end(); i = to<Data>(dataCurrentPtr)->training.upper_bound(i->first))
				modelTypeSeq.push_back(i->first);
			grasp::StringSeq::iterator modelTypePtr = modelTypeSeq.end();
			select(modelTypePtr, modelTypeSeq.begin(), modelTypeSeq.end(), "Select type:\n  [0]  Create new type\n", [] (grasp::StringSeq::const_iterator ptr) -> const std::string& { return *ptr; });
			modelType = *modelTypePtr;
		}
		catch (const golem::Message&) {
			readString("Enter type: ", modelType);
		}

		// compute reference frame and adjust object frame
		const data::Location3D* location = is<data::Location3D>(ptr);
		if (!location)
			throw Message(Message::LEVEL_ERROR, "Object handler does not implement data::Location3D");
		Vec3Seq points;
		for (size_t i = 0; i < location->getNumOfLocations(); ++i)
			points.push_back(location->getLocation(i));
		data::Location3D::Point::Seq pointsTrn;
		pointsTrn.resize(points.size());
		Mat34 frame = forwardTransformArm(lookupState()), frameInv;
		frameInv.setInverse(frame);

		// run model tools
		const std::string options("Press a key to: add (C)ontact/(T)rajectory data, finish <Enter>...");
		context.write("%s\n", options.c_str());
		for (bool finish = false; !finish;) {
			// attach object to the robot's end-effector
			frame = forwardTransformArm(lookupState());
			const Mat34 trn = frame * frameInv; // frame = trn * frameInit, trn = frame * frameInit^-1
			for (size_t i = 0; i < points.size(); ++i)
				trn.multiply(pointsTrn[i], points[i]);
			{
				golem::CriticalSectionWrapper csw(csRenderer);
				objectRenderer.reset();
				for (size_t i = 0; i < pointsTrn.size(); ++i)
					objectRenderer.addPoint(pointsTrn[i], objectFrameAdjustment.colourSolid);
			}

			// model options
			const int key = waitKey(20);
			switch (key) {
			case 'C': {
				context.write("%s )C(\n", options.c_str());
				// clone model object item
				data::Item::Ptr item = ptr->second->clone();
				data::Feature3D* features = is<data::Feature3D>(item.get());
				if (!features)
					throw Message(Message::LEVEL_ERROR, "Object handler does not implement data::Feature3D");
				// transform
				features->transform(frame * frameInv);
				// select model
				grasp::Model::Map::const_iterator model = modelMap.find(modelType);
				if (model == modelMap.end()) model = modelAny;
				context.write("Using model: %s\n", model->first.c_str());
				// create contact training data
				const grasp::Vec3Seq& vertices = to<Data>(dataCurrentPtr)->modelVertices;
				const grasp::TriangleSeq& triangles = to<Data>(dataCurrentPtr)->modelTriangles;
				grasp::Contact3D::Triangle::Seq modelMesh;
				for (grasp::TriangleSeq::const_iterator j = triangles.begin(); j != triangles.end(); ++j)
					modelMesh.push_back(Contact3D::Triangle(vertices[j->t1], vertices[j->t2], vertices[j->t3]));
				Contact3D::Seq contacts;
				contacts.clear();
				if (model->second->create(*features, to<Data>(dataCurrentPtr)->modelFrame, modelMesh, contacts)) {
					golem::CriticalSectionWrapper cswData(csData);
					Data::Training training(lookupState());
					training.contacts = contacts;
					training.frame = to<Data>(dataCurrentPtr)->modelFrame;
					training.locations = pointsTrn;
					to<Data>(dataCurrentPtr)->setTrainingItem(to<Data>(dataCurrentPtr)->training.insert(to<Data>(dataCurrentPtr)->training.end(), std::make_pair(modelType, training)));
					createRender();
				}
				// done here
				context.write("Done!\n");
				break;
			}
			case 'T': {
				context.write("%s )T(\n", options.c_str());
				// add trajectory waypoint
				const std::string trjName = getTrajectoryName(modelItemTrj, modelType);
				grasp::data::Item::Map::iterator ptr = to<Data>(dataCurrentPtr)->itemMap.find(trjName);
				if (ptr == to<Data>(dataCurrentPtr)->itemMap.end()) {
					golem::CriticalSectionWrapper cswData(csData);
					ptr = to<Data>(dataCurrentPtr)->itemMap.insert(to<Data>(dataCurrentPtr)->itemMap.end(), data::Item::Map::value_type(trjName, modelHandlerTrj->create()));
				}
				data::Trajectory* trajectory = is<data::Trajectory>(ptr);
				if (!trajectory)
					throw Message(Message::LEVEL_ERROR, "Trajectory handler does not implement data::Trajectory");
				// add current state
				Controller::State::Seq seq = trajectory->getWaypoints();
				seq.push_back(lookupState());
				trajectory->setWaypoints(seq);
				// done here
				context.write("Done!\n");
				break;
			}
			case 13: finish = true; break;
			//case 27: throw Cancel("Cancelled");
			}
		}

		context.write("Done!\n");
	}));
	menuCmdMap.insert(std::make_pair("PMR", [=]() {
		if (to<Data>(dataCurrentPtr)->training.empty())
			throw Message(Message::LEVEL_ERROR, "Empty training data");

		const bool bModelType = option("TC", "Remove (T)ype/(C)ontact... ") == 'T';
		// load model object item
		{
			RenderBlock renderBlock(*this);
			golem::CriticalSectionWrapper cswData(csData);
			Demo::Data::Training::Map::iterator ptr = to<Data>(dataCurrentPtr)->getTrainingItem();
			const std::string modelType = ptr->first; // cache
			if (bModelType)
				to<Data>(dataCurrentPtr)->training.erase(ptr->first);
			else
				to<Data>(dataCurrentPtr)->training.erase(ptr);
			// remove trajectory if no items of a given type are present
			//if (to<Data>(dataCurrentPtr)->training.end() == to<Data>(dataCurrentPtr)->training.find(modelType))
			//	to<Data>(dataCurrentPtr)->itemMap.erase(getTrajectoryName(modelItemTrj, modelType));
			createRender();
		}

		context.write("Done!\n");
	}));

	// query operations
	menuCtrlMap.insert(std::make_pair("PQ", [=](MenuCmdMap& menuCmdMap, std::string& desc) {
		// set mode
		to<Data>(dataCurrentPtr)->mode = Data::MODE_QUERY;
		createRender();

		desc = "Press a key to: create (D)ensities, generate (S)olutions ...";
	}));
	menuCmdMap.insert(std::make_pair("PQD", [=]() {
		// load object item
		data::Item::Map::iterator ptr = to<Data>(dataCurrentPtr)->itemMap.find(objectItem);
		if (ptr == to<Data>(dataCurrentPtr)->itemMap.end())
			throw Message(Message::LEVEL_ERROR, "Object item has not been created");
		// wrist frame
		const Mat34 frame = forwardTransformArm(lookupState());
		// create query densities
		createQuery(ptr->second, frame);
		createRender();

		context.write("Done!\n");
	}));
	menuCmdMap.insert(std::make_pair("PQS", [=]() {
		// generate wrist pose solutions
		generateSolutions();

		to<Data>(dataCurrentPtr)->mode = Data::MODE_SOLUTION;
		createRender();

		context.write("Done!\n");
	}));

	// trajectory operations
	menuCtrlMap.insert(std::make_pair("PT", [=](MenuCmdMap& menuCmdMap, std::string& desc) {
		// set mode
		to<Data>(dataCurrentPtr)->mode = Data::MODE_SOLUTION;
		createRender();

		desc = "Press a key to: trajectory (S)elect/(P)erform...";
	}));
	menuCmdMap.insert(std::make_pair("PTS", [=]() {
		// select best trajectory
		selectTrajectory();
		createRender();

		context.write("Done!\n");
	}));
	menuCmdMap.insert(std::make_pair("PTP", [=]() {
		// perform trajectory
		performTrajectory(true);
		createRender();

		context.write("Done!\n");
	}));

	////////////////////////////////////////////////////////////////////////////
	//                            MAIN DEMO                                   // 
	////////////////////////////////////////////////////////////////////////////

	menuCmdMap.insert(std::make_pair("PD", [=]() {
		// debug mode
		const bool stopAtBreakPoint = option("YN", "Debug mode (Y/N)...") == 'Y';
		const auto breakPoint = [=] (const char* str) {
			if (stopAtBreakPoint) {
				if (option("YN", makeString("%s: Continue (Y/N)...", str).c_str()) != 'Y')
					throw Cancel("Demo cancelled");
			}
			else {
				context.write("%s\n", str);
				(void)waitKey(0);
			}
		};

		// estimate pose
		if (to<Data>(dataCurrentPtr)->queryVertices.empty() || to<Data>(dataCurrentPtr)->queryVertices.empty()) {
			breakPoint("Dishwasher pose estimation");
			estimatePose(Data::MODE_QUERY);
		}

		// run demo
		for (;;) {
			// grasp and scan object
			breakPoint("Object grasp and point cloud capture");
			grasp::data::Item::Map::iterator ptr = objectGraspAndCapture(stopAtBreakPoint);

			breakPoint("Action planning");

			// compute features and add to data bundle
			ptr = objectProcess(ptr);
			// wrist frame
			const Mat34 frame = forwardTransformArm(lookupState());
			// create query densities
			createQuery(ptr->second, frame);
			// generate wrist pose solutions
			generateSolutions();
			// select best trajectory
			selectTrajectory();

			breakPoint("Action execution");
			// execute trajectory
			performTrajectory(stopAtBreakPoint);
		}
		context.write("Done!\n");

	}));

	////////////////////////////////////////////////////////////////////////////
	//                               UTILITIES                                // 
	////////////////////////////////////////////////////////////////////////////

	menuCtrlMap.insert(std::make_pair("Z", [=](MenuCmdMap& menuCmdMap, std::string& desc) {
		desc = "Press a key to: run (R)otate, (N)udge, (O)bjectGraspAndCapture, rgb-to-ir (T)ranform, (D)epth camera adjust, create (P)oses, (L)ocate object...";
	}));

	menuCmdMap.insert(std::make_pair("ZO", [=]() {
		context.write("Testing objectGraspAndCapture()\n");
		objectGraspAndCapture(true);
		context.write("Done!\n");
	}));

	menuCmdMap.insert(std::make_pair("ZR", [=]() {
		context.write("rotate object in hand\n");
		rotateObjectInHand();
		context.write("Done!\n");
	}));

	menuCmdMap.insert(std::make_pair("ZN", [=]() {
		context.write("nudge wrist\n");
		nudgeWrist();
		context.write("Done!\n");
	}));

	menuCmdMap.insert(std::make_pair("ZT", [=]() {
		context.write("compute rgb-to-ir tranform\n");

		try
		{
			std::string fileRGB, fileIR;
			readString("File[.cal] with extrinsic for rgb: ", fileRGB);
			readString("File[.cal] with extrinsic for ir: ", fileIR);

			XMLParser::Ptr pParserRGB = XMLParser::load(fileRGB + ".cal");
			XMLParser::Ptr pParserIR = XMLParser::load(fileIR + ".cal");

			Mat34 cameraFrame, invCameraFrame, depthCameraFrame, colourToIRFrame;
			XMLData(cameraFrame, pParserRGB->getContextRoot()->getContextFirst("grasp sensor extrinsic"));
			XMLData(depthCameraFrame, pParserIR->getContextRoot()->getContextFirst("grasp sensor extrinsic"));

			invCameraFrame.setInverse(cameraFrame);
			colourToIRFrame.multiply(invCameraFrame, depthCameraFrame);

			XMLParser::Ptr pParser = XMLParser::Desc().create();
			XMLData(colourToIRFrame, pParser->getContextRoot()->getContextFirst("grasp sensor colourToIRFrame", true), true);
			XMLData(cameraFrame, pParser->getContextRoot()->getContextFirst("grasp sensor cameraFrame", true), true);
			XMLData(depthCameraFrame, pParser->getContextRoot()->getContextFirst("grasp sensor depthCameraFrame", true), true);
			FileWriteStream fws("colourToIRFrame.xml");
			pParser->store(fws);

			context.write("Saved transform in colourToIRFrame.xml\n");
		}
		catch (const Message& e)
		{
			context.write("%s\nFailed to compute transform!\n", e.what());
		}
	}));

	menuCmdMap.insert(std::make_pair("ZD", [=]() {
		RBAdjust rba;
		rba.increment.set(golem::Real(0.0005), golem::REAL_PI*golem::Real(0.001)); // small increments

		select(sensorCurrentPtr, sensorMap.begin(), sensorMap.end(), "Select Sensor:\n",
			[](Sensor::Map::const_iterator ptr) -> const std::string& {return ptr->second->getID();} );

		if (!is<CameraDepth>(sensorCurrentPtr))
			throw Cancel("Only works for depth cameras");

		CameraDepth* camera = is<CameraDepth>(sensorCurrentPtr);
		const Mat34 trn0 = camera->getColourToIRFrame();

		context.write(
			"Use adjustment keys %s %s %s or 0 for identity. To finish press <SPACE> or <ESC>\n",
			rba.linKeys.c_str(), rba.angKeys.c_str(), rba.incKeys.c_str());
		for (;;)
		{
			const int k = waitKey(golem::MSEC_TM_U32_INF);
			if (k == 27) // <Esc>
			{
				camera->setColourToIRFrame(trn0);
				throw Cancel("Cancelled");
			}
			if (k == 32) // <Space>
				break;
			if (k == '0')
			{
				camera->setColourToIRFrame(Mat34::identity());
				continue;
			}
			if (rba.adjustIncrement(k))
			{
				const RBDist& incr = rba.getIncrement();
				context.write("increment lin: %f, ang: %f\n", incr.lin, incr.ang);
				continue;
			}
			
			Mat34 trn = camera->getColourToIRFrame();
			rba.adjustFrame(k, trn);
			camera->setColourToIRFrame(trn);
		}

		const Mat34 trn = camera->getColourToIRFrame();
		const Mat34 cameraFrame = camera->getFrame();
		Mat34 depthCameraFrame;
		depthCameraFrame.multiply(cameraFrame, trn);
		context.write("<colourToIRFrame %s></colourToIRFrame>\n", toXMLString(trn).c_str());
		context.write("<extrinsic %s></extrinsic>\n", toXMLString(depthCameraFrame).c_str());
		context.write("Done!\n");
	}));

	menuCmdMap.insert(std::make_pair("ZP", [=]() {
		context.write("create arm configs for a set of camera frames\n");
		double theta(50), phi1(-180), phi2(160), phiStep(20), R(0.45), cx(0.45), cy(-0.45), cz(-0.30);
		readNumber("theta ", theta);
		readNumber("phi1 ", phi1);
		readNumber("phi2 ", phi2);
		readNumber("phiStep ", phiStep);
		readNumber("R ", R);
		readNumber("cx ", cx);
		readNumber("cy ", cy);
		readNumber("cz ", cz);

		// create a set of poses at co-latitude theta, from phi1 to phi2 (step phiStep)
		// looking at (cx,cy,cz) at a distance R
		std::vector<Mat34> cameraPoses;
		grasp::ConfigMat34::Seq configs;
		const Vec3 centre(cx, cy, cz), Zaxis(0.0,0.0,1.0);
		Vec3 cameraCentre, cameraXdir, cameraYdir, cameraZdir;
		Mat34 cameraFrame, wristPose;
		const double degToRad = golem::REAL_PI / 180.0;
		const double sintheta = sin(theta * degToRad);
		const double costheta = cos(theta * degToRad);
		for (double phi = phi1; phi <= phi2; phi += phiStep)
		{
			const double sinphi = sin(phi * degToRad);
			const double cosphi = cos(phi * degToRad);
			const Vec3 ray(cosphi*sintheta, sinphi*sintheta, costheta);
			cameraCentre.multiply(R, ray);
			cameraCentre += centre;
			cameraZdir.multiply(-1.0, ray);
			cameraXdir.cross(cameraZdir, Zaxis);
			cameraXdir.normalise();
			cameraYdir.cross(cameraZdir, cameraXdir);
			cameraFrame.R = Mat33(cameraXdir, cameraYdir, cameraZdir);
			cameraFrame.p = cameraCentre;
			cameraPoses.push_back(cameraFrame);
			objectRenderer.addAxes3D(cameraFrame, golem::Vec3(0.05));

			//Mat33 m;
			//m.setTransposed(cameraFrame.R);
			//m.multiply(m, cameraFrame.R);
			//context.debug("%s\n", toXMLString(Mat34(m,Vec3::zero())).c_str());

			// transform from camera to wrist frame
			// default calibration for wrist extrinsics
			Mat34 trn(
				Mat33(-0.015082, -0.0709979, 0.997362, 0.999767, 0.0143117, 0.0161371, -0.0154197, 0.997374, 0.0707655),
				Vec3(0.0920632, -0.0388034, 0.161098));
			grasp::Camera* camera = getWristCamera(true);
			if (camera != nullptr)
				trn = getWristCamera()->getCurrentCalibration()->getParameters().pose;

			Mat34 invTrn;
			invTrn.setInverse(trn);
			wristPose = cameraFrame * invTrn;
			// get config from wrist pose, by executing trajectory from current pose!!!
			gotoWristPose(wristPose);
			grasp::ConfigMat34 cfg;
			getPose(0, cfg);
			configs.push_back(cfg);
		}

		// write out configs in xml format to file
		std::string filePoses("scan_poses.xml");
		readString("Filename for scan poses: ", filePoses);
		FileWriteStream fws(filePoses.c_str());

		std::ostringstream os;
		bool first(true);
		for (auto i = configs.begin(); i != configs.end(); ++i, first=false)
		{
			if (!first) os << "\n";
			os << "  <pose name=\"scan_poses\" dim=\"61\" " << toXMLString(*i) << "/>";
		}
		os << std::ends;
		fws.write(os.str().c_str(), os.str().size()-1);

		context.write("Done!\n");
	}));

	menuCmdMap.insert(std::make_pair("ZL", [=]() {
		context.write("locate object\n");

		std::string itemName("transformed_image_for_locate");
		
		data::Item::Map& itemMap = to<Data>(dataCurrentPtr)->itemMap;

		if (itemMap.empty())
			throw Message(Message::LEVEL_ERROR, "No items");

		// get current item as a data::Item::Map::iterator ptr
		data::Item::Map::iterator ptr = itemMap.begin(); // @@@ should use current item @@@

		data::ItemImage* itemImage = to<data::ItemImage>(ptr->second.get());

		// generate features
		data::Transform* transform = is<data::Transform>(&ptr->second->getHandler());
		if (!transform)
			throw Message(Message::LEVEL_ERROR, "Current item does not support Transform interface");
		data::Item::List list;
		list.insert(list.end(), ptr);
		data::Item::Ptr item = transform->transform(list);
		{
			golem::CriticalSectionWrapper cswData(csData);
			itemMap.erase(itemName);
			ptr = itemMap.insert(itemMap.end(), data::Item::Map::value_type(itemName, item));
			Data::View::setItem(itemMap, ptr, to<Data>(dataCurrentPtr)->getView());
		}

		// estimate pose
		data::Model* model = is<data::Model>(ptr);
		if (!model)
			throw Message(Message::LEVEL_ERROR, "Transformed item does not support Model interface");
		grasp::ConfigMat34 robotPose;
		golem::Mat34 modelFrame;
		model->model(robotPose, modelFrame);

		context.write("Model pose is: %s\n", toXMLString(modelFrame).c_str());

		const int k = option("MC", "Update (M)odel pose or (C)amera extrinsics for some .cal file, or (Q)uit");
		if (k == 'C')
		{
			Mat34 cameraFrame, newCameraFrame, invModelFrame, trueModelFrame;
			invModelFrame.setInverse(modelFrame);

			// get camera frame of image F (should be extrinsics of depth camera)
			cameraFrame = itemImage->parameters.pose;

			std::string fileCal("GraspCameraOpenNIChest.cal");
			readString("Calibration filename: ", fileCal);

			// assume true model pose is in .cal file
			XMLParser::Ptr pParser = XMLParser::load(fileCal);
			XMLData(trueModelFrame, pParser->getContextRoot()->getContextFirst("grasp sensor extrinsic model pose"));

			context.write("Camera frame for image was:  %s\n", toXMLString(cameraFrame).c_str());
			context.write("Assuming true model pose is: %s\n", toXMLString(trueModelFrame).c_str());

			// new extrinsics = trueModelFrame * modelFrame^-1 * cameraFrame
			newCameraFrame.multiply(invModelFrame, cameraFrame);
			context.write("transform from model to camera wrt model frame: %s\n", toXMLString(newCameraFrame).c_str());
			newCameraFrame.multiply(trueModelFrame, newCameraFrame);

			context.write("Updating %s with\n<extrinsic %s></extrinsic>\n", fileCal.c_str(), toXMLString(newCameraFrame).c_str());
			
			// current static camera on Boris:
			// m11 = "-0.689817" m12 = "-0.351147" m13 = "0.633124" m21 = "-0.723807" m22 = "0.353741" m23 = "-0.592427" m31 = "-0.015934" m32 = "-0.866928" m33 = "-0.498180"
			// v1 = "0.216904" v2 = "-0.058540" v3 = "0.281856"

			// write new extrinsics into .cal xml
			FileWriteStream fws(fileCal.c_str());
			XMLData(newCameraFrame, pParser->getContextRoot()->getContextFirst("grasp sensor extrinsic", true), true);
			pParser->store(fws); // @@@ save does not work
		}
		else if (k == 'M')
		{
			std::string fileCal("GraspCameraOpenNIChest.cal");
			readString("Calibration filename: ", fileCal);

			XMLParser::Ptr pParser = XMLParser::load(fileCal);
			XMLData(modelFrame, pParser->getContextRoot()->getContextFirst("grasp sensor extrinsic model pose", true), true);
			FileWriteStream fws(fileCal.c_str());
			pParser->store(fws); // @@@ save does not work
		}

		context.write("Done!\n");
	}));

	// END OF MENUS

	}

//------------------------------------------------------------------------------

grasp::data::Item::Map::iterator pacman::Demo::estimatePose(Data::Mode mode) {
	if (mode != Data::MODE_MODEL && (to<Data>(dataCurrentPtr)->modelVertices.empty() || to<Data>(dataCurrentPtr)->modelTriangles.empty()))
		throw Cancel("Model has not been estimated");

	grasp::Vec3Seq& vertices = mode != Data::MODE_MODEL ? to<Data>(dataCurrentPtr)->queryVertices : to<Data>(dataCurrentPtr)->modelVertices;
	grasp::TriangleSeq& triangles = mode != Data::MODE_MODEL ? to<Data>(dataCurrentPtr)->queryTriangles : to<Data>(dataCurrentPtr)->modelTriangles;
	golem::Mat34& frame = mode != Data::MODE_MODEL ? to<Data>(dataCurrentPtr)->queryFrame : to<Data>(dataCurrentPtr)->modelFrame;
	const std::string itemName = mode != Data::MODE_MODEL ? queryItem : modelItem;
	grasp::data::Handler* handler = mode != Data::MODE_MODEL ? queryHandler : modelHandler;
	grasp::Camera* camera = mode != Data::MODE_MODEL ? queryCamera : modelCamera;

	// set mode
	to<Data>(dataCurrentPtr)->mode = mode;

	// run robot
	gotoPose(modelScanPose);

	// block keyboard and mouse interaction
	InputBlock inputBlock(*this);
	{
		RenderBlock renderBlock(*this);
		golem::CriticalSectionWrapper cswData(csData);
		to<Data>(dataCurrentPtr)->itemMap.erase(itemName);
		vertices.clear();
		triangles.clear();
	}
	// capture and insert data
	data::Capture* capture = is<data::Capture>(handler);
	if (!capture)
		throw Message(Message::LEVEL_ERROR, "Handler %s does not support Capture interface", handler->getID().c_str());
	data::Item::Map::iterator ptr;
	data::Item::Ptr item = capture->capture(*camera, [&](const grasp::TimeStamp*) -> bool { return true; });
	{
		RenderBlock renderBlock(*this);
		golem::CriticalSectionWrapper cswData(csData);
		to<Data>(dataCurrentPtr)->itemMap.erase(itemName);
		ptr = to<Data>(dataCurrentPtr)->itemMap.insert(to<Data>(dataCurrentPtr)->itemMap.end(), data::Item::Map::value_type(itemName, item));
		Data::View::setItem(to<Data>(dataCurrentPtr)->itemMap, ptr, to<Data>(dataCurrentPtr)->getView());
	}
	// generate features
	data::Transform* transform = is<data::Transform>(handler);
	if (!transform)
		throw Message(Message::LEVEL_ERROR, "Handler %s does not support Transform interface", handler->getID().c_str());
	data::Item::List list;
	list.insert(list.end(), ptr);
	item = transform->transform(list);
	{
		RenderBlock renderBlock(*this);
		golem::CriticalSectionWrapper cswData(csData);
		to<Data>(dataCurrentPtr)->itemMap.erase(itemName);
		ptr = to<Data>(dataCurrentPtr)->itemMap.insert(to<Data>(dataCurrentPtr)->itemMap.end(), data::Item::Map::value_type(itemName, item));
		Data::View::setItem(to<Data>(dataCurrentPtr)->itemMap, ptr, to<Data>(dataCurrentPtr)->getView());
	}
	// estimate pose
	data::Model* model = is<data::Model>(ptr);
	if (!model)
		throw Message(Message::LEVEL_ERROR, "Item %s does not support Model interface", ptr->first.c_str());
	grasp::ConfigMat34 robotPose;
	golem::Mat34 modelFrame, modelNewFrame;
	Vec3Seq modelVertices;
	TriangleSeq modelTriangles;
	model->model(robotPose, modelFrame, &modelVertices, &modelTriangles);

	// compute a new frame
	if (mode == Data::MODE_MODEL) {
		Vec3Seq modelPoints;
		Rand rand(context.getRandSeed());
		Import().generate(rand, modelVertices, modelTriangles, [&](const golem::Vec3& p, const golem::Vec3&) { modelPoints.push_back(p); });
		modelNewFrame = RBPose::createFrame(modelPoints);
	}

	// create triangle mesh
	{
		RenderBlock renderBlock(*this);
		golem::CriticalSectionWrapper cswData(csData);
		vertices = modelVertices;
		triangles = modelTriangles;
		if (mode == Data::MODE_MODEL) {
			// newFrame = modelFrame * offset ==> offset = modelFrame^-1 * newFrame
			frame = modelNewFrame;
			to<Data>(dataCurrentPtr)->modelFrameOffset.setInverse(modelFrame);
			to<Data>(dataCurrentPtr)->modelFrameOffset.multiply(to<Data>(dataCurrentPtr)->modelFrameOffset, modelNewFrame);
		}
		else
			frame = modelFrame * to<Data>(dataCurrentPtr)->modelFrameOffset;
	}

	return ptr;
}

//------------------------------------------------------------------------------

// move robot to grasp open pose, wait for force event, grasp object (closed pose), move through scan poses and capture object, add as objectScan
// 1. move robot to grasp open pose
// 2. wait for force event; reset bias and wait for change > threshold (set in xml)
// 3. grasp object (closed pose - generate in virtual env; strong enough for both plate and cup; only fingers should move)
// full hand grasp, fingers spread out; thumb in centre
// 4. move through scan poses and capture object, add as objectScan
// return ptr to Item
grasp::data::Item::Map::iterator pacman::Demo::objectGraspAndCapture(const bool stopAtBreakPoint)
{
	const auto breakPoint = [=](const char* str) {
		if (stopAtBreakPoint) {
			if (option("YN", makeString("%s: Continue (Y/N)...", str).c_str()) != 'Y')
				throw Cancel("Demo cancelled");
		}
		else {
			context.write("%s\n", str);
			(void)waitKey(0);
		}
	};

	gotoPose(graspPoseOpen);

	context.write("Waiting for force event, simulate (F)orce event or <ESC> to cancel\n");
	ForceEvent forceEvent(graspSensorForce, graspThresholdForce);
	forceEvent.setBias();
	for (;;)
	{
		const int k = waitKey(10); // poll FT every 10ms
		if (k == 27) // <Esc>
			throw Cancel("Cancelled");
		if (k == 'F')
		{
			context.debug("Simulated force event\n");
			break;
		}

		if (forceEvent.detected(&context))
			break;
	}

	context.debug("Closing hand!\n");
	gotoPose2(graspPoseClosed, graspCloseDuration);

	Sleep::msleep(SecToMSec(graspEventTimeWait));

	breakPoint("Go to scan pose");

	context.debug("Proceeding to first scan pose!\n");
	gotoPose(objectScanPoseSeq.front());

	data::Capture* capture = is<data::Capture>(objectHandlerScan);
	if (!capture)
		throw Message(Message::LEVEL_ERROR, "Handler %s does not support Capture interface", objectHandlerScan->getID().c_str());

	RenderBlock renderBlock(*this);
	data::Item::Map::iterator ptr;
	{
		golem::CriticalSectionWrapper cswData(csData);
		data::Item::Ptr item = capture->capture(*objectCamera, [&](const grasp::TimeStamp*) -> bool { return true; });

		// Finally: insert object scan, remove old one
		data::Item::Map& itemMap = to<Data>(dataCurrentPtr)->itemMap;
		itemMap.erase(objectItemScan);
		ptr = itemMap.insert(itemMap.end(), data::Item::Map::value_type(objectItemScan, item));
		Data::View::setItem(itemMap, ptr, to<Data>(dataCurrentPtr)->getView());
	}

	return ptr;
}

//------------------------------------------------------------------------------

// Process object image and add to data bundle
grasp::data::Item::Map::iterator pacman::Demo::objectProcess(grasp::data::Item::Map::iterator ptr) {
	// generate features
	data::Transform* transform = is<data::Transform>(objectHandler);
	if (!transform)
		throw Message(Message::LEVEL_ERROR, "Handler %s does not support Transform interface", objectHandler->getID().c_str());

	data::Item::List list;
	list.insert(list.end(), ptr);
	data::Item::Ptr item = transform->transform(list);

	// insert processed object, remove old one
	RenderBlock renderBlock(*this);
	golem::CriticalSectionWrapper cswData(csData);
	to<Data>(dataCurrentPtr)->itemMap.erase(objectItem);
	ptr = to<Data>(dataCurrentPtr)->itemMap.insert(to<Data>(dataCurrentPtr)->itemMap.end(), data::Item::Map::value_type(objectItem, item));
	Data::View::setItem(to<Data>(dataCurrentPtr)->itemMap, ptr, to<Data>(dataCurrentPtr)->getView());
	return ptr;
}

//------------------------------------------------------------------------------

std::string pacman::Demo::getTrajectoryName(const std::string& prefix, const std::string& type) const {
	return prefix + dataDesc->sepName + type;
}

//------------------------------------------------------------------------------

void pacman::Demo::createQuery(grasp::data::Item::Ptr item, const golem::Mat34& frame) {
	// Features
	const data::Feature3D* features = is<data::Feature3D>(item.get());
	if (!features)
		throw Message(Message::LEVEL_ERROR, "Demo::createQuery(): No query features");
	// training data are required
	if (to<Data>(dataCurrentPtr)->training.empty())
		throw Message(Message::LEVEL_ERROR, "Demo::createQuery(): No model densities");
	// model object pose
	if (to<Data>(dataCurrentPtr)->modelVertices.empty() || to<Data>(dataCurrentPtr)->modelVertices.empty())
		throw Message(Message::LEVEL_ERROR, "Demo::createQuery(): No model pose");
	// query object pose
	if (to<Data>(dataCurrentPtr)->queryVertices.empty() || to<Data>(dataCurrentPtr)->queryVertices.empty())
		throw Message(Message::LEVEL_ERROR, "Demo::createQuery(): No query pose");

	// select query any
	const grasp::Query::Map::const_iterator queryAny = queryMap.find(ID_ANY);
	if (queryAny == queryMap.end())
		throw Message(Message::LEVEL_ERROR, "Demo::createQuery(): Unable to find Query %s", ID_ANY.c_str());
	// select pose std dev any
	const PoseDensity::Map::const_iterator poseAny = poseMap.find(ID_ANY);
	if (poseAny == poseMap.end())
		throw Message(Message::LEVEL_ERROR, "Demo::createQuery(): Unable to find pose density %s", ID_ANY.c_str());

	to<Data>(dataCurrentPtr)->densities.clear();
	for (Data::Training::Map::const_iterator i = to<Data>(dataCurrentPtr)->training.begin(); i != to<Data>(dataCurrentPtr)->training.end(); ++i) {
		// select query density
		grasp::Query::Map::const_iterator query = queryMap.find(i->first);
		if (query == queryMap.end()) query = queryAny;

		try {
			query->second->clear();
			//if (!golem::Sample<golem::Real>::normalise<golem::Ref1>(const_cast<grasp::Contact3D::Seq&>(i->second.contacts)))
			//	throw Message(Message::LEVEL_ERROR, "Demo::createQuery(): Unable to normalise model distribution");
			query->second->create(i->second.contacts, *features);
		}
		catch (const std::exception& ex) {
			context.write("%s\n", ex.what());
			continue;
		}

		Data::Density density;
		density.type = i->first;

		// object density
		density.object = query->second->getPoses();
		for (grasp::Query::Pose::Seq::iterator j = density.object.begin(); j != density.object.end(); ++j) {
			Mat34 trn;

			// frame transform: eff_curr = frame, model = modelFrame
			// model = trn * query |==> trn = model * query^-1
			// eff_pred = trn * eff_curr |==> eff_pred = model * query^-1 * eff_curr
			trn.setInverse(j->toMat34());
			trn.multiply(trn, frame);
			trn.multiply(to<Data>(dataCurrentPtr)->queryFrame, trn);
			j->fromMat34(trn);
		}

		// select pose any
		PoseDensity::Map::const_iterator pose = poseMap.find(i->first);
		if (pose == poseMap.end()) pose = poseAny;

		// trajectory
		const std::string trjName = getTrajectoryName(modelItemTrj, i->first);
		grasp::data::Item::Map::iterator ptr = to<Data>(dataCurrentPtr)->itemMap.find(trjName);
		if (ptr == to<Data>(dataCurrentPtr)->itemMap.end())
			throw Message(Message::LEVEL_ERROR, "Demo::createQuery(): Empty trajectory");
		data::Trajectory* trajectory = is<data::Trajectory>(ptr);
		if (!trajectory)
			throw Message(Message::LEVEL_ERROR, "Demo::createQuery(): Trajectory handler does not implement data::Trajectory");
		// waypoints
		const golem::Controller::State::Seq waypoints = trajectory->getWaypoints();
		if (waypoints.size() < 2)
			throw Message(Message::LEVEL_ERROR, "Demo::createQuery(): Trajectory must have at least two waypoints");

		// Desired trajectory frame at contact pose
		const golem::Mat34 trajectoryFrame = forwardTransformArm(i->second.state);
		// Contact and approach poses as recorded
		const golem::Mat34 contactPose(forwardTransformArm(waypoints[0])), approachPose(forwardTransformArm(waypoints[1]));
		// trajectoryFrame = trn * contactPose |==> trn = trajectoryFrame * contactPose^-1
		golem::Mat34 trnTrj;
		trnTrj.setInverse(contactPose);
		trnTrj.multiply(trajectoryFrame, trnTrj);

		// query = trn * model |==> trn = query * model^-1
		golem::Mat34 trn;
		trn.setInverse(to<Data>(dataCurrentPtr)->modelFrame);
		trn.multiply(to<Data>(dataCurrentPtr)->queryFrame, trn);

		// Contact and approach poses in the desired frame
		const grasp::RBCoord contactFrame(trn * trajectoryFrame), approachFrame(trn * trnTrj * approachPose);

		// distance
		const RBDist frameDist(contactFrame.p.distance(approachFrame.p), contactFrame.q.distance(approachFrame.q));
		if (frameDist.lin < REAL_EPS || frameDist.ang < REAL_EPS)
			throw Message(Message::LEVEL_ERROR, "Demo::createQuery(): Invalid distance between waypoints");

		// create pose distribution
		const I32 range = pose->second.kernels / 2 + 1;
		density.pose.reserve(2 * range + 1);
		for (I32 j = -range; j <= range; ++j) {
			const Real dist = Real(j)/range;

			// kernel
			grasp::Query::Pose qp;
			
			// interpolation factor
			const grasp::RBDist interpol(dist * pose->second.pathDist.lin / frameDist.lin, dist * pose->second.pathDist.ang / frameDist.ang);

			// linear interpolation/extrapolation
			qp.p.interpolate(contactFrame.p, approachFrame.p, interpol.lin);
			// angular interpolation/extrapolation, TODO use angular distance scaling
			qp.q.slerp(contactFrame.q, approachFrame.q, interpol.lin);

			// kernel parameters
			qp.stdDev = pose->second.stdDev;
			qp.cov.set(Math::sqr(qp.stdDev.lin), Math::sqr(qp.stdDev.ang));
			qp.covInv.set(REAL_ONE / qp.cov.lin, REAL_ONE / qp.cov.ang);
			qp.distMax.set(poseDistanceMax * qp.cov.lin, poseDistanceMax * qp.cov.ang);
			// kernel weight
			qp.weight = Math::exp( - Math::abs(dist)*pose->second.pathDistStdDev);

			// add to pose ditribution
			density.pose.push_back(qp);
		}

		// normalise pose ditribution
		if (!golem::Sample<Real>::normalise<golem::Ref1>(density.pose))
			throw Message(Message::LEVEL_ERROR, "Demo::createQuery(): Unable to normalise pose distribution");

		// create path
		density.path = manipulator->create(waypoints, [=](const Manipulator::Waypoint& l, const Manipulator::Waypoint& r) -> Real { return poseCovInv.dot(RBDist(l, r)); });
		
		// locations
		Mat34 trnObj;
		trnObj.setInverse(frame);
		density.locations.clear();
		density.locations.reserve(features->getNumOfLocations());
		for (size_t i = 0; i < features->getNumOfLocations(); ++i) {
			grasp::data::Location3D::Point p = features->getLocation(i);
			trnObj.multiply(p, p);
			density.locations.push_back(p);
		}

		// end-effector frame
		density.frame = trajectoryFrame;
		
		// likelihood
		density.weight = query->second->weight;

		// done
		to<Data>(dataCurrentPtr)->densities.push_back(density);
	}

	if (to<Data>(dataCurrentPtr)->densities.empty())
		throw Message(Message::LEVEL_ERROR, "Demo::createQuery(): No query created");

	if (!golem::Sample<Real>::normalise<golem::Ref1>(to<Data>(dataCurrentPtr)->densities))
		throw Message(Message::LEVEL_ERROR, "Demo::createQuery(): Unable to normalise query distributions");
}

void pacman::Demo::generateSolutions() {
	// training data are required
	if (to<Data>(dataCurrentPtr)->densities.empty())
		throw Message(Message::LEVEL_ERROR, "Demo::generateSolutions(): No query densities");

	const SecTmReal t = context.getTimer().elapsed();

	Mat34 referencePose;
	referencePose.setInverse(manipulator->getBaseFrame());

	size_t acceptGreedy = 0, acceptSA = 0;

	to<Data>(dataCurrentPtr)->solutions.resize(optimisation.runs);
	Data::Solution::Seq::iterator ptr = to<Data>(dataCurrentPtr)->solutions.begin();
	CriticalSection cs;
	ParallelsTask(context.getParallels(), [&](ParallelsTask*) {
		const U32 jobId = context.getParallels()->getCurrentJob()->getJobId();
		Rand rand(RandSeed(this->context.getRandSeed()._U32[0] + jobId, (U32)0));

		Data::Solution *solution = nullptr, test;

		for (;;) {
			// select next pointer
			{
				CriticalSectionWrapper csw(cs);
				if (ptr == to<Data>(dataCurrentPtr)->solutions.end())
					break;
				solution = &*ptr++;
			}

			// sample query density
			Data::Density::Seq::const_iterator query;
			for (;;) {
				query = golem::Sample<golem::Real>::sample<golem::Ref1, Data::Density::Seq::const_iterator>(to<Data>(dataCurrentPtr)->densities, rand);
				if (query == to<Data>(dataCurrentPtr)->densities.end()) {
					context.error("Demo::generateSolutions(): Query density sampling error\n");
					return;
				}
				break;
			}
			// sample pose density
			grasp::Query::Pose::Seq::const_iterator pose;
			for (;;) {
				pose = golem::Sample<golem::Real>::sample<golem::Ref1, grasp::Query::Pose::Seq::const_iterator>(query->pose, rand);
				if (pose == query->pose.end()) {
					context.error("Demo::generateSolutions(): Pose density sampling error\n");
					return;
				}
				break;
			}
			// set
			test.likelihood.setToDefault();
			test.type = query->type;
			test.queryIndex = (U32)(query - to<Data>(dataCurrentPtr)->densities.begin());

			// local search: try to find better solution using simulated annealing
			for (size_t s = 0; s <= optimisation.steps; ++s) {
				const bool init = s == 0;

				// linear schedule
				const Real Scale = Real(optimisation.steps - s)/optimisation.steps; // 0..1
				const Real Temp = (REAL_ONE - Scale)*optimisation.saTemp + Scale;
				const RBDist Delta(optimisation.saDelta.lin*Temp, optimisation.saDelta.ang*Temp);
				const Real Energy = optimisation.saEnergy*Temp;

				// generate test solution
				for (;;) {
					// Linear component
					Vec3 v;
					v.next(rand); // |v|==1
					v.multiply(Math::abs(rand.nextGaussian<Real>(REAL_ZERO, Delta.lin*pose->stdDev.lin)), v);
					test.pose.p.add(init ? pose->p : solution->pose.p, v);
					// Angular component
					const Real poseCovInvAng = pose->covInv.ang / Math::sqr(Delta.ang);
					Quat q;
					q.next(rand, poseCovInvAng);
					test.pose.q.multiply(init ? pose->q : solution->pose.q, q);

					// create path
					test.path = query->path;

					// transform to the new frame
					RBCoord inv;
					inv.setInverse(test.path[0]);
					for (Manipulator::Waypoint::Seq::iterator i = test.path.begin(); i != test.path.end(); ++i) {
						i->multiply(inv, *i);
						i->multiply(test.pose * referencePose, *i);
					}

					// evaluate
					if (!Data::Solution::Likelihood::isValid(test.likelihood.contact = evaluateSample(query->object.begin(), query->object.end(), test.pose)))
						continue;
					if (!Data::Solution::Likelihood::isValid(test.likelihood.pose = evaluateSample(query->pose.begin(), query->pose.end(), test.pose)))
						continue;
					if (!Data::Solution::Likelihood::isValid(test.likelihood.collision = golem::numeric_const<golem::Real>::ONE))
						continue;

					test.likelihood.make();
					//test.likelihood.makeLog();

					break;
				}

				// first run sampling only
				if (init) {
					*solution = test;
					continue;
				}

				// accept if better
				if (test.likelihood.likelihood > solution->likelihood.likelihood || Math::exp((test.likelihood.likelihood - solution->likelihood.likelihood) / Energy) > rand.nextUniform<Real>()) {
					// debug
					test.likelihood.likelihood > solution->likelihood.likelihood ? ++acceptGreedy : ++acceptSA;
					// update
					solution->pose = test.pose;
					solution->path = test.path;
					solution->likelihood = test.likelihood;
				}
			}
		}
	});

	// sort
	sortSolutions(to<Data>(dataCurrentPtr)->solutions);

	// print debug information
	context.debug("Demo::generateSolutions(): time=%.6f, solutions=%u, steps=%u, energy=%f, greedy_accept=%d, SA_accept=%d\n", context.getTimer().elapsed() - t, optimisation.runs, optimisation.steps, optimisation.saEnergy, acceptGreedy, acceptSA);
}

void pacman::Demo::sortSolutions(Data::Solution::Seq& seq) const {
	if (seq.empty())
		throw Message(Message::LEVEL_ERROR, "Demo::sortSolutions(): No solutions");

	// create pointers
	typedef std::vector<const Data::Solution*> PtrSeq;
	PtrSeq ptrSeq;
	ptrSeq.reserve(seq.size());
	for (Data::Solution::Seq::const_iterator i = seq.begin(); i != seq.end(); ++i)
		ptrSeq.push_back(&*i);

	// sort with clustering
	std::sort(ptrSeq.begin(), ptrSeq.end(), [](const Data::Solution* l, const Data::Solution* r) -> bool {return l->queryIndex < r->queryIndex || l->queryIndex == r->queryIndex && l->likelihood.likelihood > r->likelihood.likelihood; });

	// copy
	//Data::Solution::Seq seqSorted;
	//seqSorted.reserve(ptrSeq.size());
	//for (PtrSeq::const_iterator i = ptrSeq.begin(); i != ptrSeq.end(); ++i)
	//	seqSorted.push_back(**i);
	Data::Solution::Seq seqSorted;
	seqSorted.reserve(ptrSeq.size());
	U32 clusterSize = manipulator->getDesc().trajectoryClusterSize, clusterId = ptrSeq.front()->queryIndex, clusterIndex = 0;
	for (PtrSeq::const_iterator i = ptrSeq.begin(); i != ptrSeq.end();) {
		seqSorted.push_back(**i++);
		if (++clusterIndex >= clusterSize) {
			for (; i != ptrSeq.end(); ++i)
				if (clusterId != (*i)->queryIndex) {
					clusterId = (*i)->queryIndex;
					clusterIndex = 0;
					break;
				}
		}
		else if (clusterId != (*i)->queryIndex) {
			clusterId = (*i)->queryIndex;
			clusterIndex = 0;
			break;
		}
	}

	// replace
	seq = seqSorted;
}

void pacman::Demo::selectTrajectory() {
	if (to<Data>(dataCurrentPtr)->solutions.empty())
		throw Message(Message::LEVEL_ERROR, "Demo::selectTrajectory(): No solutions");

	const U32 testTrajectories = (U32)to<Data>(dataCurrentPtr)->solutions.size();

	// TODO collision detection
	// collision bounds
	//CollisionBounds collisionBounds(const_cast<golem::Planner&>(manipulator->getPlanner()), [=] (size_t i, Vec3& p)->bool {
	//	return false;
	//}, &objectRenderer, &csRenderer);
	//collisionBounds.setLocal();

	// search
	const std::pair<U32, RBDistEx> val = manipulator->find<U32>(0, testTrajectories, [&](U32 index) -> RBDistEx {
		to<Data>(dataCurrentPtr)->indexSolution = index;
		createRender();
		Manipulator::Waypoint::Seq& path = to<Data>(dataCurrentPtr)->solutions[index].path;
		const RBDist dist(manipulator->find(path));
		const golem::ConfigspaceCoord approach = manipulator->getConfig(path.back());
		const bool collides = false;// collisionBounds.collides(approach, manipulator->getArm()->getStateInfo().getJoints().end() - 1); // TODO test approach config using locations
		const RBDistEx distex(dist, manipulator->getDesc().trajectoryErr.collision && collides);
		context.write("#%03u/%u: Trajectory error: lin=%.9f, ang=%.9f, collision=%s\n", index + 1, testTrajectories, distex.lin, distex.ang, distex.collision ? "yes" : "no");
		//if (getUICallback() && getUICallback()->hasInputEnabled()) Menu(context, *getUICallback()).option("\x0D", "Press <Enter> to continue...");
		return distex;
	});

	to<Data>(dataCurrentPtr)->indexSolution = val.first;
	context.write("#%03u: Best trajectory\n", val.first + 1);
	createRender();
	Controller::State::Seq seq;
	manipulator->copy(to<Data>(dataCurrentPtr)->solutions[val.first].path, seq);

	// add trajectory waypoint
	{
		RenderBlock renderBlock(*this);
		golem::CriticalSectionWrapper cswData(csData);
		to<Data>(dataCurrentPtr)->itemMap.erase(queryItemTrj);
		grasp::data::Item::Map::iterator ptr = to<Data>(dataCurrentPtr)->itemMap.insert(to<Data>(dataCurrentPtr)->itemMap.end(), data::Item::Map::value_type(queryItemTrj, modelHandlerTrj->create()));
		data::Trajectory* trajectory = is<data::Trajectory>(ptr);
		if (!trajectory)
			throw Message(Message::LEVEL_ERROR, "Demo::selectTrajectory(): Trajectory handler does not implement data::Trajectory");
		// add current state
		trajectory->setWaypoints(seq);
		Data::View::setItem(to<Data>(dataCurrentPtr)->itemMap, ptr, to<Data>(dataCurrentPtr)->getView());
	}
}

void pacman::Demo::performTrajectory(bool testTrajectory) {
	grasp::data::Item::Map::iterator ptr = to<Data>(dataCurrentPtr)->itemMap.find(queryItemTrj);
	if (ptr == to<Data>(dataCurrentPtr)->itemMap.end())
		throw Message(Message::LEVEL_ERROR, "Demo::performTrajectory(): Unable to find query trajectory");
	data::Trajectory* trajectory = is<data::Trajectory>(ptr);
	if (!trajectory)
		throw Message(Message::LEVEL_ERROR, "Demo::performTrajectory(): Trajectory handler does not implement data::Trajectory");

	Controller::State::Seq seqInv = trajectory->getWaypoints();
	if (seqInv.size() < 2)
		throw Message(Message::LEVEL_ERROR, "Demo::performTrajectory(): At least two waypoints required");
	// reverse
	Controller::State::Seq seq;
	for (Controller::State::Seq::const_reverse_iterator i = seqInv.rbegin(); i != seqInv.rend(); ++i) seq.push_back(*i);
	
	// profile
	struct ProfileCallback : Profile::CallbackDist {
		const Demo* demo;
		ProfileCallback(const Demo* demo) : demo(demo) {}
		Real distConfigspaceCoord(const ConfigspaceCoord& prev, const ConfigspaceCoord& next) const {
			RBCoord cprev(demo->forwardTransformArm(prev)), cnext(demo->forwardTransformArm(next));
			return Math::sqrt(demo->poseCovInv.dot(RBDist(cprev, cnext)));
		}
		Real distCoord(Real prev, Real next) const {
			return Math::abs(prev - next);
		}
		bool distCoordEnabled(const Configspace::Index& index) const {
			return true;
		}
	} profileCallback(this);
	Profile::Desc desc;
	desc.pCallbackDist = &profileCallback;
	//auto pDesc = new Polynomial4::Desc; // 4-th deg polynomial - quadratic velocity
	auto pDesc = new Polynomial1::Desc; // 1-st deg polynomial - constant velocity
	desc.pTrajectoryDesc.reset(pDesc);
	Profile::Ptr profile = desc.create(*controller);
	if (profile == nullptr)
		throw Message(Message::LEVEL_ERROR, "Demo::performTrajectory(): unable to create profile");
	seq.back().t = seq.front().t + manipulatorTrajectoryDuration;
	profile->profile(seq);

	golem::Controller::State::Seq initTrajectory;
	findTrajectory(lookupState(), &seq.front(), nullptr, SEC_TM_REAL_ZERO, initTrajectory);

	golem::Controller::State::Seq completeTrajectory = initTrajectory;
	completeTrajectory.insert(completeTrajectory.end(), seq.begin(), seq.end());

	// create trajectory item
	data::Item::Ptr itemTrajectory;
	data::Handler::Map::const_iterator handlerPtr = handlerMap.find(trajectoryHandler);
	if (handlerPtr == handlerMap.end())
		throw Message(Message::LEVEL_ERROR, "Demo::performTrajectory(): unknown default trajectory handler %s", trajectoryHandler.c_str());
	data::Handler* handler = is<data::Handler>(handlerPtr);
	if (!handler)
		throw Message(Message::LEVEL_ERROR, "Demo::performTrajectory(): invalid default trajectory handler %s", trajectoryHandler.c_str());
	itemTrajectory = handler->create();
	data::Trajectory* trajectoryIf = is<data::Trajectory>(itemTrajectory.get());
	if (!trajectoryIf)
		throw Message(Message::LEVEL_ERROR, "Demo::performTrajectory(): unable to create trajectory using handler %s", trajectoryHandler.c_str());
	trajectoryIf->setWaypoints(completeTrajectory);
	
	// remove if failed
	bool finished = false;
	const Data::View view = to<Data>(dataCurrentPtr)->getView();
	ScopeGuard removeItem([&]() {
		if (!finished) {
			RenderBlock renderBlock(*this);
			golem::CriticalSectionWrapper csw(csData);
			to<Data>(dataCurrentPtr)->itemMap.erase(manipulatorItemTrj);
			to<Data>(dataCurrentPtr)->getView() = view;
		}
	});
	// add trajectory item
	{
		RenderBlock renderBlock(*this);
		golem::CriticalSectionWrapper csw(csData);
		const data::Item::Map::iterator ptr = to<Data>(dataCurrentPtr)->itemMap.insert(to<Data>(dataCurrentPtr)->itemMap.end(), data::Item::Map::value_type(manipulatorItemTrj, itemTrajectory));
		Data::View::setItem(to<Data>(dataCurrentPtr)->itemMap, ptr, to<Data>(dataCurrentPtr)->getView());
	}

	// test trajectory
	if (testTrajectory) {
		// prompt user
		EnableKeyboardMouse enableKeyboardMouse(*this);
		option("\x0D", "Press <Enter> to accept trajectory...");
	}

	// block displaying the current item
	RenderBlock renderBlock(*this);

	// go to initial state
	setHandConfig(initTrajectory, objectScanPoseSeq.back());
	sendTrajectory(initTrajectory);
	// wait until the last trajectory segment is sent
	controller->waitForEnd();

	Sleep::msleep(SecToMSec(0.5)); // wait before taking bias for F/T
	ForceEvent forceEvent(graspSensorForce, trajectoryThresholdForce);
	forceEvent.setBias();

	// send trajectory
	setHandConfig(seq, objectScanPoseSeq.back());
	sendTrajectory(seq);

	// repeat every send waypoint until trajectory end
	for (U32 i = 0; controller->waitForBegin(); ++i)
	{
		// ~20ms loop
		if (universe.interrupted())
			throw Exit();
		if (controller->waitForEnd(0))
			break;

		bool spoofedForceEvent = false;
		if (waitKey(0) == 'F')
		{
			context.debug("Simulated force event\n");
			spoofedForceEvent = true;
		}

		if (forceEvent.detected(&context) || spoofedForceEvent)
		{
			context.debug("Force event detected. Halting robot.\n");
			haltRobot();
			controller->waitForBegin();
			break;
			// jiggle, if not docked => when bottom of object is near base of rack
		}

		// print every 10th robot state
		if (i % 10 == 0)
			context.write("State #%d\r", i);
	}
	context.debug("\n");

	// open hand and perform withdraw action to a safe pose
	// translate wrist vertically upwards, keeping orientation constant

	context.debug("Releasing hand by %g%% in %gs...\n", withdrawReleaseFraction*100.0, trajectoryDuration);
	releaseHand(withdrawReleaseFraction, trajectoryDuration);

	context.debug("Waiting 1s before withdrawing upwards...\n");
	Sleep::msleep(SecToMSec(1.0));

	context.debug("Lifting hand by %gm in %gs...\n", withdrawLiftDistance, trajectoryDuration);
	liftWrist(withdrawLiftDistance, trajectoryDuration);

	// done
	finished = true;
	context.write("Performance finished!\n");
}

//------------------------------------------------------------------------------

void pacman::Demo::render() const {
	Player::render();
	
	golem::CriticalSectionWrapper cswRenderer(csRenderer);
	modelRenderer.render();
	objectRenderer.render();
}

//------------------------------------------------------------------------------

void golem::XMLData(pacman::Demo::PoseDensity::Map::value_type& val, golem::XMLContext* xmlcontext, bool create) {
	golem::XMLData("id", const_cast<std::string&>(val.first), xmlcontext, create);
	val.second.load(xmlcontext->getContextFirst("pose", create));
}

//------------------------------------------------------------------------------

template <> void golem::Stream::read(pacman::Demo::Data::Training::Map::value_type& value) const {
	read(const_cast<std::string&>(value.first));
	read(value.second.state);
	read(value.second.frame);
	value.second.contacts.clear();
	read(value.second.contacts, value.second.contacts.begin());
	value.second.locations.clear();
	read(value.second.locations, value.second.locations.begin());
}

template <> void golem::Stream::write(const pacman::Demo::Data::Training::Map::value_type& value) {
	write(value.first);
	write(value.second.state);
	write(value.second.frame);
	write(value.second.contacts.begin(), value.second.contacts.end());
	write(value.second.locations.begin(), value.second.locations.end());
}

template <> void golem::Stream::read(pacman::Demo::Data::Density::Seq::value_type& value) const {
	read(value.type);
	value.object.clear();
	read(value.object, value.object.begin());
	value.pose.clear();
	read(value.pose, value.pose.begin());
	value.path.clear();
	read(value.path, value.path.begin());
	value.locations.clear();
	read(value.locations, value.locations.begin());
	read(value.frame);
	read(value.weight);
	read(value.cdf);
}

template <> void golem::Stream::write(const pacman::Demo::Data::Density::Seq::value_type& value) {
	write(value.type);
	write(value.object.begin(), value.object.end());
	write(value.pose.begin(), value.pose.end());
	write(value.path.begin(), value.path.end());
	write(value.locations.begin(), value.locations.end());
	write(value.frame);
	write(value.weight);
	write(value.cdf);
}

template <> void golem::Stream::read(pacman::Demo::Data::Solution::Seq::value_type& value) const {
	read(value.type);
	read(value.pose);
	value.path.clear();
	read(value.path, value.path.begin());
	read(value.likelihood);
	read(value.queryIndex);
}

template <> void golem::Stream::write(const pacman::Demo::Data::Solution::Seq::value_type& value) {
	write(value.type);
	write(value.pose);
	write(value.path.begin(), value.path.end());
	write(value.likelihood);
	write(value.queryIndex);
}

//------------------------------------------------------------------------------

int main(int argc, char *argv[]) {
	return pacman::Demo::Desc().main(argc, argv);
}
