#include <pacman/Bham/Demo/Demo.h>

#include <Golem/Math/Rand.h>
#include <Grasp/Grasp/Model.h>
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
	for (size_t i = 0; i < cfg.c.size(); ++i)
	{
		const double v = (shortFormat && i>6) ? 0.0 : cfg.c[i];
		os << (i==0 ? "c" : " c") << i+1 << """" << v << """";
	}

	return os.str();
}
}

//-----------------------------------------------------------------------------

const std::string Demo::ID_ANY = "Any";

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

	queryMode = false;
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
		const grasp::Vec3Seq& vertices = queryMode ? queryVertices : modelVertices;
		const grasp::TriangleSeq& triangles = queryMode ? queryTriangles : modelTriangles;
		const golem::Mat34& frame = queryMode ? queryFrame : modelFrame;
		owner->modelRenderer.setColour(owner->modelColourSolid);
		owner->modelRenderer.addSolid(vertices.data(), (U32)vertices.size(), triangles.data(), (U32)triangles.size());
		owner->modelRenderer.setColour(owner->modelColourWire);
		owner->modelRenderer.addWire(vertices.data(), (U32)vertices.size(), triangles.data(), (U32)triangles.size());
		if (!vertices.empty() && !triangles.empty())
			owner->modelRenderer.addAxes3D(frame, Vec3(0.2));
		// training data
		if (!queryMode) {
			owner->contactAppearance.relation = contactRelation;
			Training::Map::iterator ptr = getTrainingItem();
			if (ptr != training.end())
				grasp::Contact3D::draw(owner->contactAppearance, ptr->second.contacts, modelFrame, owner->modelRenderer);
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

	manipulatorDesc->load(xmlcontext->getContextFirst("manipulator"));
	manipulatorAppearance.load(xmlcontext->getContextFirst("manipulator appearance"));
}

//------------------------------------------------------------------------------

pacman::Demo::Demo(Scene &scene) : 
	Player(scene),
	modelCamera(nullptr), queryCamera(nullptr), modelHandler(nullptr), queryHandler(nullptr), graspSensorForce(nullptr), objectCamera(nullptr), objectHandlerScan(nullptr), objectHandler(nullptr)
{}

pacman::Demo::~Demo() {
}

//------------------------------------------------------------------------------

grasp::Camera* Demo::getWristCamera() const
{
	const std::string id("OpenNI+OpenNI");
	grasp::Sensor::Map::const_iterator i = sensorMap.find(id);
	if (i == sensorMap.end())
	{
		context.write("%s was not found\n", id.c_str());
		throw Cancel("getWristCamera: wrist-mounted camera is not available");
	}

	grasp::Camera* camera = grasp::is<grasp::Camera>(i);

	// want the wrist-mounted camera
	if (!camera->hasVariableMounting())
	{
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

golem::Controller::State::Seq Demo::getTrajectoryFromPose(const golem::Mat34& w)
{
	const golem::Mat34 R = controller->getChains()[armInfo.getChains().begin()]->getReferencePose();
	const golem::Mat34 wR = w * R;

	golem::Controller::State begin = controller->createState();
	controller->lookupState(SEC_TM_REAL_MAX, begin);

	golem::Controller::State::Seq trajectory;
	const grasp::RBDist err = findTrajectory(begin, nullptr, &wR, trajectoryDuration, trajectory);

	return trajectory;
}

grasp::ConfigMat34 Demo::getConfigFromPose(const golem::Mat34& w)
{
#if DONE
	golem::Controller::State::Seq trajectory = getTrajectoryFromPose(w);
	return ConfigMat34(trajectory.back().cpos);
#else
	return ConfigMat34();
#endif
}

void Demo::gotoWristPose(const golem::Mat34& w)
{
	golem::Controller::State::Seq trajectory = getTrajectoryFromPose(w);
	sendTrajectory(trajectory);
	controller->waitForEnd();
	Sleep::msleep(SecToMSec(trajectoryIdleEnd));
}

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

	grasp::Camera* camera = getWristCamera();
	pose = camera->getFrame();
	showPose("camera before", pose);

	pose = getWristPose();
	showPose("before", pose);

	pose.p = pose.p + nudge;
	showPose("commanded", pose);

	gotoWristPose(pose);

	showPose("final wrist", getWristPose());
	pose = camera->getFrame();
	showPose("final camera", pose);

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

	switch(rotType)
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

	// manipulator
	manipulator = desc.manipulatorDesc->create(*planner, desc.controllerIDSeq);
	manipulatorAppearance = desc.manipulatorAppearance;

	// top menu help using global key '?'
	scene.getHelp().insert(Scene::StrMapVal("0F5", "  P                                       menu PaCMan\n"));
	scene.getHelp().insert(Scene::StrMapVal("0F5", "  Z                                       menu SZ Tests\n"));
	scene.getHelp().insert(Scene::StrMapVal("0F5", "  <Tab>                                   Query/Model mode switch\n"));
	scene.getHelp().insert(Scene::StrMapVal("0F5", "  ()                                      Training item selection\n"));

	menuCmdMap.insert(std::make_pair("\t", [=]() {
		// set mode
		to<Data>(dataCurrentPtr)->queryMode = !to<Data>(dataCurrentPtr)->queryMode;
		context.write("%s mode\n", to<Data>(dataCurrentPtr)->queryMode ? "Query" : "Model");
		createRender();
	}));
	itemSelect = [&](ItemSelectFunc itemSelectFunc) {
		if (!to<Data>(dataCurrentPtr)->queryMode) {
			if (to<Data>(dataCurrentPtr)->training.empty()) {
				context.write("No training data\n");
				return;
			}
			Data::Training::Map::iterator ptr = to<Data>(dataCurrentPtr)->getTrainingItem();
			itemSelectFunc(to<Data>(dataCurrentPtr)->training, ptr);
			to<Data>(dataCurrentPtr)->setTrainingItem(ptr);
			context.write("Type %s/%u, Item #%u\n", ptr->first.c_str(), to<Data>(dataCurrentPtr)->indexType + 1, to<Data>(dataCurrentPtr)->indexItem + 1);
			createRender();
		}
	};
	menuCmdMap.insert(std::make_pair("(", [&]() {
		itemSelect([] (Data::Training::Map& map, Data::Training::Map::iterator& ptr) {
			if (ptr == map.begin()) ptr = --map.end(); else --ptr;
		});
	}));
	menuCmdMap.insert(std::make_pair(")", [&]() {
		itemSelect([] (Data::Training::Map& map, Data::Training::Map::iterator& ptr) {
			if (ptr == --map.end()) ptr = map.begin(); else ++ptr;
		});
	}));

	// data menu control and commands
	menuCtrlMap.insert(std::make_pair("P", [=](MenuCmdMap& menuCmdMap, std::string& desc) {
		desc = "Press a key to: (E)stimate pose, (C)apture object, (M)odel/(Q)uery menu, run (D)emo ...";
	}));

	// model pose estimation
	menuCtrlMap.insert(std::make_pair("PE", [=](MenuCmdMap& menuCmdMap, std::string& desc) {
		desc = "Press a key to: (M)odel/(Q)ery estimation...";
	}));
	menuCmdMap.insert(std::make_pair("PEM", [=]() {
		// estimate
		(void)estimatePose(false);
		// finish
		context.write("Done!\n");
	}));
	menuCmdMap.insert(std::make_pair("PEQ", [=]() {
		// estimate
		(void)estimatePose(true);
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
		to<Data>(dataCurrentPtr)->queryMode = false;
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
				const std::string trjName = getTrajectoryName(modelType);
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
			//	to<Data>(dataCurrentPtr)->itemMap.erase(getTrajectoryName(modelType));
			createRender();
		}

		context.write("Done!\n");
	}));

	// query operations
	menuCtrlMap.insert(std::make_pair("PQ", [=](MenuCmdMap& menuCmdMap, std::string& desc) {
		// set mode
		to<Data>(dataCurrentPtr)->queryMode = true;
		createRender();

		desc = "Press a key to: create (D)ensities, generate (S)olutions ...";
	}));
	menuCmdMap.insert(std::make_pair("PQD", [=]() {
		// load object item
		data::Item::Map::iterator ptr = to<Data>(dataCurrentPtr)->itemMap.find(objectItem);
		if (ptr == to<Data>(dataCurrentPtr)->itemMap.end())
			throw Message(Message::LEVEL_ERROR, "Object item has not been created");
		// create query densities
		createQuery(ptr);

		context.write("Done!\n");
	}));
	menuCmdMap.insert(std::make_pair("PQS", [=]() {

		context.write("Done!\n");
	}));

	// main demo
	menuCmdMap.insert(std::make_pair("PD", [=]() {
		// estimate pose
		if (to<Data>(dataCurrentPtr)->queryVertices.empty() || to<Data>(dataCurrentPtr)->queryVertices.empty())
			estimatePose(true);

		// run demo
		for (;;) {
			// grasp and scan object
			grasp::data::Item::Map::iterator ptr = objectGraspAndCapture();
			// compute features and add to data bundle
			ptr = objectProcess(ptr);
			// create query densities
			createQuery(ptr);
		}
		context.write("Done!\n");

	}));

	menuCtrlMap.insert(std::make_pair("Z", [=](MenuCmdMap& menuCmdMap, std::string& desc) {
		desc = "Press a key to: run (R)otate, (N)udge, (O)bjectGraspAndCapture, rgb-to-ir (T)ranform, (D)epth camera adjust, create (P)oses...";
	}));

	menuCmdMap.insert(std::make_pair("ZO", [=]() {
		context.write("Testing objectGraspAndCapture()\n");
		objectGraspAndCapture();
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
		double theta(60), phi1(0), phi2(150), phiStep(10), R(0.50), cx(0.45), cy(-0.40), cz(-0.30);
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
			cameraYdir.cross(cameraZdir, cameraXdir);
			cameraFrame.R = Mat33(cameraXdir, cameraYdir, cameraZdir);
			cameraFrame.p = cameraCentre;
			cameraPoses.push_back(cameraFrame);
			objectRenderer.addAxes3D(cameraFrame, golem::Vec3(0.05));
			// transform from camera to wrist frame
			const Mat34& trn = getWristCamera()->getCurrentCalibration()->getParameters().pose;
			Mat34 invTrn;
			invTrn.setInverse(trn);
			wristPose = cameraFrame * invTrn;
			// get config from wrist pose, by planning trajectory from current pose!
			grasp::ConfigMat34 cfg = getConfigFromPose(wristPose);
			configs.push_back(cfg);
		}
		// write out configs in xml format to scan_poses.xml

		context.write("Done!\n");
	}));

	}

//------------------------------------------------------------------------------

grasp::data::Item::Map::iterator pacman::Demo::estimatePose(bool queryMode) {
	if (queryMode && (to<Data>(dataCurrentPtr)->modelVertices.empty() || to<Data>(dataCurrentPtr)->modelTriangles.empty()))
		throw Cancel("Model has not been estimated");

	grasp::Vec3Seq& vertices = queryMode ? to<Data>(dataCurrentPtr)->queryVertices : to<Data>(dataCurrentPtr)->modelVertices;
	grasp::TriangleSeq& triangles = queryMode ? to<Data>(dataCurrentPtr)->queryTriangles : to<Data>(dataCurrentPtr)->modelTriangles;
	golem::Mat34& frame = queryMode ? to<Data>(dataCurrentPtr)->queryFrame : to<Data>(dataCurrentPtr)->modelFrame;
	const std::string itemName = queryMode ? queryItem : modelItem;
	grasp::data::Handler* handler = queryMode ? queryHandler : modelHandler;
	grasp::Camera* camera = queryMode ? queryCamera : modelCamera;

	// set mode
	to<Data>(dataCurrentPtr)->queryMode = queryMode;

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
	if (!queryMode) {
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
		if (queryMode)
			frame = modelFrame * to<Data>(dataCurrentPtr)->modelFrameOffset;
		else {
			// newFrame = modelFrame * offset ==> offset = modelFrame^-1 * newFrame
			frame = modelNewFrame;
			to<Data>(dataCurrentPtr)->modelFrameOffset.setInverse(modelFrame);
			to<Data>(dataCurrentPtr)->modelFrameOffset.multiply(to<Data>(dataCurrentPtr)->modelFrameOffset, modelNewFrame);
		}
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
grasp::data::Item::Map::iterator pacman::Demo::objectGraspAndCapture()
{
	gotoPose(graspPoseOpen);

	context.write("Waiting for force event, simulate (F)orce event or <ESC> to cancel\n");
	SecTmReal t;
	Twist biasForce, currentForce;
	double thresholdFT[6], biasFT[6], currentFT[6];
	graspSensorForce->readSensor(biasForce, t);
	graspThresholdForce.get(thresholdFT);
	biasForce.get(biasFT);
	for (;;)
	{
		const int k = waitKey(10); // poll FT every 10ms
		if (k == 27) // <Esc>
			throw Cancel("Cancelled");
		if (k == 'F')
		{
			context.write("Simulated force event\n");
			break;
		}

		// check if force has deviated more than threshold from bias
		graspSensorForce->readSensor(currentForce, t);
		currentForce.get(currentFT);
		bool tripped = false;
		for (size_t i = 0; i < 6; ++i)
		{
			if (abs(currentFT[i] - biasFT[i]) > thresholdFT[i])
			{
				context.write("Force event detected on axis %d: |%f - %f| > %f\n", i + 1, currentFT[i], biasFT[i], thresholdFT[i]);
				tripped = true;
				break;
			}
		}
		if (tripped) break;
	}

	Sleep::msleep(SecToMSec(graspEventTimeWait));

	context.write("Closing hand!\n");
	gotoPose2(graspPoseClosed, graspCloseDuration);

	context.write("<SPACE> to continue to scan pose, or <ESC> to cancel\n");
	for (;;)
	{
		const int k = waitKey(golem::MSEC_TM_U32_INF);
		if (k == 27) // <Esc>
			throw Cancel("Cancelled");
		if (k == 32) // <Space>
			break;
	}

	context.write("Proceeding to first scan pose!\n");
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

std::string pacman::Demo::getTrajectoryName(const std::string& type) const {
	return modelItemTrj + "-" + type;
}

//------------------------------------------------------------------------------

void pacman::Demo::createQuery(grasp::data::Item::Map::iterator ptr) {
	if (to<Data>(dataCurrentPtr)->training.empty())
		throw Message(Message::LEVEL_ERROR, "No model densities");
}

//------------------------------------------------------------------------------

void pacman::Demo::render() const {
	Player::render();
	
	golem::CriticalSectionWrapper cswRenderer(csRenderer);
	modelRenderer.render();
	objectRenderer.render();
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
	value.object.clear();
	read(value.object, value.object.begin());
	read(value.pose);
}

template <> void golem::Stream::write(const pacman::Demo::Data::Density::Seq::value_type& value) {
	write(value.object.begin(), value.object.end());
	write(value.pose);
}

template <> void golem::Stream::read(pacman::Demo::Data::Solution::Seq::value_type& value) const {
	read(value.type);
	value.path.clear();
	read(value.path, value.path.begin());
}

template <> void golem::Stream::write(const pacman::Demo::Data::Solution::Seq::value_type& value) {
	write(value.type);
	write(value.path.begin(), value.path.end());
}

//------------------------------------------------------------------------------

int main(int argc, char *argv[]) {
	return pacman::Demo::Desc().main(argc, argv);
}
