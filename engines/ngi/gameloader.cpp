/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include "ngi/ngi.h"

#include "ngi/gameloader.h"
#include "ngi/scene.h"
#include "ngi/input.h"
#include "ngi/statics.h"
#include "ngi/interaction.h"
#include "ngi/motion.h"
#include "ngi/constants.h"
#include "ngi/scenes.h"
#include "ngi/floaters.h"
#include "ngi/ngiarchive.h"
#include "ngi/detection.h"

#include "common/memstream.h"

namespace NGI {

Inventory2 *getGameLoaderInventory() {
	return &g_nmi->_gameLoader->_inventory;
}

static MotionController *getMotionControllerBySceneId(int16 sceneId) {
	for (uint i = 0; i < g_nmi->_gameLoader->_sc2array.size(); i++) {
		if (g_nmi->_gameLoader->_sc2array[i]._sceneId == sceneId) {
			return g_nmi->_gameLoader->_sc2array[i]._motionController;
		}
	}

	return nullptr;
}

MovGraph *getSc2MovGraphBySceneId(int16 sceneId) {
	MotionController *mc = getMotionControllerBySceneId(sceneId);
	if (mc) {
		assert(mc->_objtype == kObjTypeMovGraph);
		return static_cast<MovGraph *>(mc);
	}
	return nullptr;
}

MctlCompound *getSc2MctlCompoundBySceneId(int16 sceneId) {
	MotionController *mc = getMotionControllerBySceneId(sceneId);
	if (mc) {
		assert(mc->_objtype == kObjTypeMctlCompound);
		return static_cast<MctlCompound *>(mc);
	}
	return nullptr;
}

InteractionController *getGameLoaderInteractionController() {
	return g_nmi->_gameLoader->_interactionController;
}

GameLoader::GameLoader() {
	_interactionController = new InteractionController();
	_inputController = new InputController();

	addMessageHandlerByIndex(global_messageHandler2, 0, 0);
	insertMessageHandler(global_messageHandler3, 0, 128);
	insertMessageHandler(global_messageHandler4, 0, 1);

	_field_FA = 0;
	_field_F8 = 0;
	_sceneSwitcher = 0;
	_preloadCallback = 0;
	_savegameCallback = 0;
	_gameVar = 0;
	_preloadSceneId = 0;
	_preloadEntranceId = 0;
	_updateCounter = 0;

	g_nmi->_msgX = 0;
	g_nmi->_msgY = 0;
	g_nmi->_msgObjectId2 = 0;
	g_nmi->_msgId = 0;
}

GameLoader::~GameLoader() {
	delete _interactionController;
	delete _inputController;
	delete _gameVar;
}

bool GameLoader::load(MfcArchive &file) {
	debugC(1, kDebugLoading, "GameLoader::load()");

	_gameName = file.readPascalString();
	debugC(1, kDebugLoading, "_gameName: %s", _gameName.c_str());

	_gameProject.reset(new GameProject());

	if (!_gameProject->load(file))
		error("Cannot load project");

	g_nmi->_gameProject = _gameProject.get();

	if (g_nmi->_gameProjectVersion < 12) {
		error("GameLoader::load(): old gameProjectVersion: %d", g_nmi->_gameProjectVersion);
	}

	_gameName = file.readPascalString();
	debugC(1, kDebugLoading, "_gameName: %s", _gameName.c_str());

	_inventory.load(file);

	_interactionController->load(file);

	debugC(1, kDebugLoading, "sceneTag count: %d", _gameProject->_sceneTagList->size());

	_sc2array.resize(_gameProject->_sceneTagList->size());

	int i = 0;
	for (SceneTagList::const_iterator it = _gameProject->_sceneTagList->begin(); it != _gameProject->_sceneTagList->end(); ++it, i++) {
		char tmp[12];

		snprintf(tmp, 11, "%04d.sc2", it->_sceneId);

		debugC(1, kDebugLoading, "sc: %s", tmp);

		_sc2array[i].loadFile(tmp);
	}

	_preloadItems.load(file);

	_field_FA = file.readUint16LE();
	_field_F8 = file.readUint16LE();

	debugC(1, kDebugLoading, "_field_FA: %d\n_field_F8: %d", _field_FA, _field_F8);

	_gameVar = file.readClass<GameVar>();

	return true;
}

bool GameLoader::loadXML(const Common::String &fname) {
	debugC(1, kDebugLoading, "GameLoader::loadXML()");

	XMLLoader *xmlLoader = new XMLLoader(fname);
	_gameVar = xmlLoader->parseXML();
	if (!_gameVar)
		return false;

	_gameName = _gameVar->getPropertyAsString("title");
	debugC(1, kDebugLoading, "_gameName: %s", _gameName.c_str());

	_gameProject.reset(new GameProject());
	_gameProject->_sceneTagList.reset(new SceneTagList());

	g_nmi->_gameProjectVersion = 12; // FIXME
	g_nmi->_gameProject = _gameProject.get();

	GameVar *gv = _gameVar->_subVars;
	while (gv != nullptr) {
		if (gv->_varName == "SCENE") {
			loadSceneXML(gv);
		} else if (gv->_varName == "PASSAGE") {
			Passage passage;
			passage.srcSceneId = gv->getPropertyAsInt("nIdSrcScene");
			passage.srcHintId = gv->getPropertyAsInt("nIdSrcHint");
			passage.destSceneId = gv->getPropertyAsInt("nIdDestScene");
			passage.destHintId = gv->getPropertyAsInt("nIdDestHint");
			_passageArray.push_back(passage);

		} else if (gv->_varName == "INTERACTIONS") {
			_interactionController->loadInteractionsFromXML(gv);
		} else if (gv->_varName == "INVENTORY") {
			_inventory.loadFromXML(gv);
		} else if (gv->_varName == "LOGIC") {
			GameVar *lv = new GameVar();
			lv->clone(gv, 1, 0);
			_logicVar = lv;
		} else if (gv->_varName == "INPUTCONTROLLER") {
			_inputController->loadFromXML(gv);
		}
		gv = gv->_nextVarObj;
	}

	return true;
}

PictureObject *LoadPicXML(GameVar *gv, const Common::String &filePrefix) {
	int id = gv->getPropertyAsInt("Id");
	Common::String fileName = Common::String::format("%s%08d.dib", filePrefix.c_str(), id);
	PictureObject *picObj = new PictureObject();
	picObj->load2(fileName);
	picObj->loadProperties(gv);
	byte alpha = (byte)gv->getPropertyAsInt("nAlpha");
	if (alpha)
		picObj->_picture->setAlpha(alpha);
	picObj->setOXY2();
	return picObj;
}

Statics *LoadStaticsXML(GameVar *gv, const Common::String &filePrefix) {
	if (gv->getPropertyAsInt("nIdMirror"))
		return nullptr;
	int id = gv->getPropertyAsInt("id");
	Common::String fileName = Common::String::format("%s%08d.dib", filePrefix.c_str(), id);
	Statics *statics = new Statics();
	statics->load2(fileName);
	statics->_staticsName = gv->getPropertyAsString("title");
	statics->_staticsId = id;
	return statics;
}

ExCommand *LoadStateXML(GameVar *gv) {
	int kind = gv->getPropertyAsInt("iId");
	if (kind == 63) {
		ObjstateCommand *objstateCmd = new ObjstateCommand();
		objstateCmd->_objCommandName = gv->getPropertyAsString("sObject");
		objstateCmd->_value = gv->getPropertyAsInt("dwState");
		objstateCmd->_messageKind = 63;
		objstateCmd->_parentId = gv->getPropertyAsInt("oWho");
		objstateCmd->_x = gv->getPropertyAsInt("cpXY.x");
		objstateCmd->_y = gv->getPropertyAsInt("cpXY.y");
		objstateCmd->_sceneClickX = gv->getPropertyAsInt("cpXYStep.x");
		objstateCmd->_sceneClickY = gv->getPropertyAsInt("cpXYStep.y");
		objstateCmd->_field_30 = gv->getPropertyAsInt("cpReserved.x");
		objstateCmd->_field_34 = gv->getPropertyAsInt("cpReserved.y");
		objstateCmd->_z = gv->getPropertyAsInt("iZ");
		objstateCmd->_invId = gv->getPropertyAsInt("iZStep");
		objstateCmd->_param = gv->getPropertyAsInt("iReserved");
		objstateCmd->_field_2C = gv->getPropertyAsInt("iReserved2");
		objstateCmd->_messageNum = gv->getPropertyAsInt("iNum");
		objstateCmd->_excFlags = gv->getPropertyAsInt("dwFlags");
		objstateCmd->_parId = gv->getPropertyAsInt("dwParent");
		objstateCmd->_field_24 = gv->getPropertyAsInt("bWait");
		objstateCmd->_field_3C = gv->getPropertyAsInt("bFree");
		return objstateCmd;
	}
	ExCommand *exCmd;
	exCmd = new ExCommand();
	exCmd->_messageKind = kind;
	exCmd->_parentId = gv->getPropertyAsInt("oWho");
	exCmd->_x = gv->getPropertyAsInt("cpXY.x");
	exCmd->_y = gv->getPropertyAsInt("cpXY.y");
	exCmd->_sceneClickX = gv->getPropertyAsInt("cpXYStep.x");
	exCmd->_sceneClickY = gv->getPropertyAsInt("cpXYStep.y");
	exCmd->_field_30 = gv->getPropertyAsInt("cpReserved.x");
	exCmd->_field_34 = gv->getPropertyAsInt("cpReserved.y");
	exCmd->_z = gv->getPropertyAsInt("iZ");
	exCmd->_invId = gv->getPropertyAsInt("iZStep");
	exCmd->_param = gv->getPropertyAsInt("iReserved");
	exCmd->_field_2C = gv->getPropertyAsInt("iReserved2");
	exCmd->_messageNum = gv->getPropertyAsInt("iNum");
	exCmd->_excFlags = gv->getPropertyAsInt("dwFlags");
	exCmd->_parId = gv->getPropertyAsInt("dwParent");
	exCmd->_field_24 = gv->getPropertyAsInt("bWait");
	exCmd->_field_3C = gv->getPropertyAsInt("bFree");
	return exCmd;
}

Movement *LoadMovementXML(GameVar *gv, const Common::String &filePrefix, StaticANIObject *aniObj) {
	if (gv->getPropertyAsInt("nIdMirror"))
		return nullptr;
	Movement *movement = new Movement();
	int id = gv->getPropertyAsInt("id");
	movement->_id = id;
	movement->_objectName = gv->getPropertyAsString("title");
	int prevId = gv->getPropertyAsInt("nIdPrev"), nextId = gv->getPropertyAsInt("nIdNext");
	for (Common::Array<Statics*>::iterator st = aniObj->_staticsList.begin(); st != aniObj->_staticsList.end(); st++) {
		if ((*st)->_staticsId == prevId)
			movement->_staticsObj1 = *st;
		if ((*st)->_staticsId == nextId)
			movement->_staticsObj2 = *st;
		if (movement->_staticsObj1 && movement->_staticsObj2)
			break;
	}
	movement->_mx = gv->getPropertyAsInt("nPrevStepX");
	movement->_my = gv->getPropertyAsInt("nPrevStepY");
	movement->_m2x = gv->getPropertyAsInt("nNextStepX");
	movement->_m2y = gv->getPropertyAsInt("nNextStepY");
	movement->_counterMax = gv->getPropertyAsInt("dwLoopDelay");
	if (gv->getPropertyAsInt("bUseAuto"))
		movement->_field_50 = 0;
	Common::String movPrefix = Common::String::format("%s%08d\\", filePrefix.c_str(), movement->_id);
	int phase = 0;
	int dynCount = gv->getPropertyAsInt("dwNumPhases");
	movement->_framePosOffsets.resize(dynCount);
	for (GameVar *k = gv->_subVars; k; k = k->_nextVarObj) {
		if (k->_varName == "PHASE") {
			movement->_framePosOffsets[phase].x = k->getPropertyAsInt("csStep.x");
			movement->_framePosOffsets[phase].y = k->getPropertyAsInt("csStep.y");
			phase++;
			Common::String fileName = Common::String::format("%s%08d.%03d", movPrefix.c_str(), movement->_id, phase);
			DynamicPhase *dynPhase = new DynamicPhase();
			dynPhase->load2(fileName);
			for (GameVar *l = k->_subVars; l; l = l->_nextVarObj) {
				if (l->_varName == "COMMAND") {
					ExCommand *exCmd = LoadStateXML(l);
					dynPhase->_exCommand.reset(exCmd);
					if (exCmd) {
						exCmd->_field_3C = 0;
					}
				}
			}
			dynPhase->_initialCountdown = k->getPropertyAsInt("iPouse");
			movement->_dynamicPhases.push_back(dynPhase);
		}
	}
	DynamicPhase *last = movement->_dynamicPhases.back();
	if (last)
		delete last;
	movement->_dynamicPhases.push_back(movement->_staticsObj2);
	return movement;
}

StaticANIObject *LoadAniXML(GameVar *gv, const Common::String &filePrefix) {
	if (gv->getPropertyAsInt("iCopy"))
		return nullptr;
	StaticANIObject *aniObj = new StaticANIObject();
	aniObj->loadProperties(gv);
	Common::String aniPrefix = Common::String::format("%s%08d\\", filePrefix.c_str(), aniObj->_id);
	for (GameVar *j = gv->_subVars; j; j = j->_nextVarObj) {
		if (j->_varName == "STATICS") {
			Statics *statics = LoadStaticsXML(j, aniPrefix);
			if (statics)
				aniObj->_staticsList.push_back(statics);
		} else if (j->_varName == "MOVEMENT") {
			Movement *movement = LoadMovementXML(j, aniPrefix, aniObj);
			if (movement)
				aniObj->_movements.push_back(movement);
		}
	}
	if (aniObj->_field_8 & 0x10000)
		aniObj->setFlags(aniObj->_flags | 4);
	return aniObj;
}

void LoadEntranceXML(EntranceInfo *entrance, GameVar *gv) {
	memset(entrance, 0, sizeof(EntranceInfo));
	Common::String title = gv->getPropertyAsString("title");
	if (!title.empty()) {
		strncpy(entrance->title, title.c_str(), 99);
	}
	Common::String entrfunct = gv->getPropertyAsString("entrfunct");
	if (!entrfunct.empty()) {
		strncpy(entrance->entrfunct, entrfunct.c_str(), 99);
	}
	entrance->_sceneId = gv->getPropertyAsInt("nIdScene");
	entrance->_field_4 = gv->getPropertyAsInt("nIdHind");
	entrance->_messageQueueId = gv->getPropertyAsInt("nIdQueue");
}

void LoadPicAniInfoXML(PicAniInfo *aniInfo, GameVar *gv) {
	aniInfo->type = gv->getPropertyAsInt("dwObjType");
	aniInfo->objectId = gv->getPropertyAsInt("nId");
	aniInfo->field_8 = gv->getPropertyAsInt("iCopy");
	aniInfo->sceneId = gv->getPropertyAsInt("nParentScene");
	aniInfo->ox = gv->getPropertyAsInt("x");
	aniInfo->oy = gv->getPropertyAsInt("y");
	aniInfo->priority = gv->getPropertyAsInt("z");
	aniInfo->staticsId = gv->getPropertyAsInt("nIdStatics");
	aniInfo->movementId = gv->getPropertyAsInt("nIdMovement");
	aniInfo->dynamicPhaseIndex = gv->getPropertyAsInt("nMovementPhase");
	aniInfo->flags = gv->getPropertyAsInt("wFlags");
	aniInfo->field_24 =gv->getPropertyAsInt("dwExFlags");
	aniInfo->someDynamicPhaseIndex = gv->getPropertyAsInt("nStopPhase");
}

ReactPolygonal *LoadReactPolygonalXML(GameVar *gv) {
	ReactPolygonal *react = new ReactPolygonal();
	int numPoints = gv->getPropertyAsInt("iNumPoints");
	react->_points.resize(numPoints);
	int point = 0;
	for (; gv; gv->_nextVarObj, point++) {
		react->_points[point].x = gv->getPropertyAsInt("x");
		react->_points[point].y = gv->getPropertyAsInt("y");
	}
	react->createRegion();
	return react;
}

MctlCompound *LoadMctlCompoundXML(GameVar *gv) {
	MctlCompound *mctlCompound = new MctlCompound();
	GameVar *sv = gv->_subVars;
	for (int i = 0; i < gv->getPropertyAsInt("nNumChildren"); i++) {
		MctlItem *mctlItem = new MctlItem();
		if (sv) {
			if (sv->_varName == "MCTLREACTZONE") {
				ReactPolygonal *react = LoadReactPolygonalXML(sv);
				mctlItem->_movGraphReactObj.reset(react);
				sv = sv->_nextVarObj;
				if (sv) {
					if (sv->_varName == "MCTLGRID") {
						MctlGrid *mctlGrid = new MctlGrid(800, 600);
						mctlItem->_motionControllerObj.reset(mctlGrid);
						mctlGrid->loadFromXML(sv);
					}
				}
			} else if (sv->_varName == "MCTLGRID") {
				MctlGrid *mctlGrid = new MctlGrid(800, 600);
				mctlItem->_motionControllerObj.reset(mctlGrid);
				mctlGrid->loadFromXML(sv);
			}
		}
		mctlCompound->_motionControllers.push_back(mctlItem);
		sv = sv->_nextVarObj;
	}
	return mctlCompound;
}

void GameLoader::loadSceneXML(GameVar *gv)
{
	Common::String xmlFile = gv->getPropertyAsString("szXmlFile");
	if (!xmlFile.empty()) {
		int sceneId = gv->getPropertyAsInt("id");
		addSceneXML(sceneId, xmlFile);
		return;
	}
	else
	{
		Scene *scene = new Scene();
		scene->_sceneName = gv->getPropertyAsString("title");
		scene->_sceneId = gv->getPropertyAsInt("id");
		scene->_lowDetailId = gv->getPropertyAsInt("LowDetailId");
		int objStateCount = gv->getSubVarsCountByName("OBJSTATE");
		int entranceCount = gv->getSubVarsCountByName("ENTRANCE");
		scene->_bigPictureXDim = gv->getPropertyAsInt("nPartsX");
		scene->_bigPictureYDim = gv->getPropertyAsInt("nPartsY");

		debugC(6, kDebugLoading, "bigPictureArray[%d][%d]", scene->_bigPictureXDim, scene->_bigPictureYDim);

		bool type = 0;
		Dims dim;
		int width, height;
		width = 0;
		for (uint i = 0; i < scene->_bigPictureXDim; ++i) {
			height = 0;
			for (uint j = 0; j < scene->_bigPictureYDim; ++j) {
				scene->_bigPictureArray.push_back(new BigPicture());
				Common::String fileName = genFileName2(scene->_sceneId, j * scene->_bigPictureXDim + i);
				scene->_bigPictureArray[i]->load2(fileName);
				scene->_bigPictureArray[i]->init();
				dim = scene->_bigPictureArray[i]->getDimensions();
				height += dim.y;
				int bitmapType = scene->_bigPictureArray[i]->getBitmap()->_type;
				if (bitmapType == MKTAG('C', 'B', 0x88, 0x88) || bitmapType == MKTAG('C', 'B', 0x80, 0x08)
					|| bitmapType == MKTAG('C', 'B', 0x08, 0x88))
					type = 1;
			}
			width += dim.x;
		}

		byte *data = (byte*)calloc(48, 1);
		
		Common::MemoryWriteStream *s = new Common::MemoryWriteStream(data + 16, 32);

		s->writeSint32LE(0); // x
		s->writeSint32LE(0); // y
		s->writeUint32LE(width); // width
		s->writeUint32LE(height); // height
		s->writeUint32LE(0); // pixels
		s->writeUint32LE(type ? MKTAG('C', 'B', 0x88, 0x88) : MKTAG('C', 'B', 0x05, 0x65)); // type
		s->writeUint32LE(0); // flags

		delete s;

		Bitmap *bitmap = new Bitmap();
		bitmap->getDibInfo(data, 48);

		PictureObject *picObj = new PictureObject();
		picObj->loadBitmap(bitmap);
		picObj->_picture->setFlag(1);
		scene->_picObjList.insert_at(0, picObj);

		Common::Array<EntranceInfo> entranceArray;
		Common::Array<PicAniInfo> objStateArray;

		entranceArray.resize(entranceCount);
		objStateArray.resize(objStateCount);

		int entrance = 0;
		int objState = 0;

		MctlCompound *mctlCompound = nullptr;

		Common::String filePrefix = Common::String::format("%08d\\", scene->_sceneId);
		for (GameVar *i = gv->_subVars; i; i = i->_nextVarObj) {
			if (i->_varName == "PICTURE") {
				PictureObject *picObj = LoadPicXML(i, filePrefix);
				scene->_picObjList.push_back(picObj);
			} else if (i->_varName == "ANI") {
				StaticANIObject *aniObj = LoadAniXML(i, filePrefix);
				if (aniObj) {
					aniObj->_sceneId = scene->_sceneId;
					scene->addStaticANIObject(aniObj, true);
				}
			} else if (i->_varName == "ENTRANCE") {
				LoadEntranceXML(&entranceArray[entrance], i);
				entrance++;
			} else if (i->_varName == "QUEUE") {
				MessageQueue *mq = new MessageQueue();
				mq->loadFromXML(i);
				scene->_messageQueueList.push_back(mq);
			} else if (i->_varName == "OBJSTATE") {
				LoadPicAniInfoXML(&objStateArray[objState], i);
				objState++;
			} else if (i->_varName == "MCTLCOMPOUND") {
				mctlCompound = LoadMctlCompoundXML(i);
			} else if (i->_varName == "DIALOGS") {
				// CDialogController
			}
		}
		// CGameSounds
		// CDialogController
		Sc2 *sc2 = findSc2(scene->_sceneId);
		if (!sc2) {
			makeSc2(scene);
			sc2 = findSc2(scene->_sceneId);
		} else {
			sc2->_scene = scene;
			_gameProject->findSceneTagById(scene->_sceneId)->_scene = scene;
		}
		sc2->_defPicAniInfos.assign(objStateArray.begin(), objStateArray.end());
		sc2->_entranceData.assign(entranceArray.begin(), entranceArray.end());
		if (sc2->_motionController)
			delete sc2->_motionController;
		sc2->_motionController = mctlCompound;
		Common::String xmlFile = gv->getPropertyAsString("szXmlFile");
		if (!xmlFile.empty()) {
			sc2->_sceneFile = xmlFile;
			if (gv) {
				delete gv;
				return;
			}
		}
		// GameVar *gridObj = gv->getSubVarByName("GRIDOBJECTSLIST");
	}
}

bool GameLoader::loadSceneXML(int sceneId) {
	SceneTag *st;

	int idx = getSceneTagBySceneId(sceneId, &st);
	if (idx < 0)
		return false;
	if (!_sc2array[idx]._scene) {
		if (_sc2array[idx]._sceneFile.empty())
			return false;
		XMLLoader *xmlLoader = new XMLLoader(_sc2array[idx]._sceneFile);
		GameVar *gv = xmlLoader->parseXML();
		Common::String archiveName = Common::String::format("%08d.nl", sceneId);

		NGIArchive *arch = makeNGIArchive(archiveName);

		loadSceneXML(gv);
		// TODO: Behavior
		_inputController->loadSceneFromXML(_sc2array[idx]._sceneId, gv);
		_sc2array[idx]._scene->_libHandle.reset(arch);
		delete gv;
	}
	return true;
}

void GameLoader::addSceneXML(int sceneId, const Common::String &fname) {
	Sc2 sc2;
	sc2._sceneId = sceneId;
	sc2._sceneFile = fname;
	_sc2array.push_back(sc2);
	SceneTag sceneTag;
	sceneTag._sceneId = sceneId;
	_gameProject->_sceneTagList->push_back(sceneTag);
}

Sc2 *GameLoader::findSc2(int sceneId) {
	for (int i = 0; i < _sc2array.size(); i++) {
		if (_sc2array[i]._sceneId == sceneId)
			return &_sc2array[i];
	}
	return nullptr;
}

void GameLoader::makeSc2(Scene *scene) {
	Sc2 sc2;
	sc2._sceneId = scene->_sceneId;
	sc2._motionController = new MctlCompound();
	sc2._scene = scene;
	sc2._sceneFile = Common::String::format("sc%08i.xml", scene->_sceneId);
	_sc2array.push_back(sc2);
	_gameProject->addSceneTag(scene);
}

bool GameLoader::loadScene(int sceneId) {
	SceneTag *st;

	int idx = getSceneTagBySceneId(sceneId, &st);

	if (idx < 0)
		return false;

	if (!st->_scene) {
		if (g_nmi->getGameGID() == GID_POPOVICH && !_sc2array[idx]._sceneFile.empty()) {
			loadSceneXML(sceneId);
		} else {
			st->loadScene();
		}
	}

	if (st->_scene) {
		st->_scene->init();

		applyPicAniInfos(st->_scene, _sc2array[idx]._defPicAniInfos);
		applyPicAniInfos(st->_scene, _sc2array[idx]._picAniInfos);

		_sc2array[idx]._scene = st->_scene;
		_sc2array[idx]._isLoaded = true;

		// TODO: Popovich dialogs

		return true;
	}

	return false;
}

bool GameLoader::gotoScene(int sceneId, int entranceId) {
	SceneTag *st;

	int sc2idx = getSceneTagBySceneId(sceneId, &st);

	if (sc2idx < 0)
		return false;

	if (!_sc2array[sc2idx]._isLoaded)
		return false;

	if (_sc2array[sc2idx]._entranceData.size() < 1) {
		g_nmi->_currentScene = st->_scene;
		return true;
	}

	if (!_sc2array[sc2idx]._entranceData.size())
		return false;

	uint entranceIdx = 0;
	if (sceneId != 726) // WORKAROUND
		for (entranceIdx = 0; _sc2array[sc2idx]._entranceData[entranceIdx]._field_4 != entranceId; entranceIdx++) {
			if (entranceIdx >= _sc2array[sc2idx]._entranceData.size())
				return false;
		}

	GameVar *sg = _gameVar->getSubVarByName("OBJSTATES")->getSubVarByName("SAVEGAME");

	if (sg || (sg = _gameVar->getSubVarByName("OBJSTATES")->addSubVarAsInt("SAVEGAME", 0)) != 0)
		sg->setSubVarAsInt("Entrance", entranceId);

	if (!g_nmi->sceneSwitcher(_sc2array[sc2idx]._entranceData[entranceIdx]))
		return false;

	g_nmi->_msgObjectId2 = 0;
	g_nmi->_msgY = -1;
	g_nmi->_msgX = -1;

	g_nmi->_currentScene = st->_scene;

	MessageQueue *mq1 = g_nmi->_currentScene->getMessageQueueById(_sc2array[sc2idx]._entranceData[entranceIdx]._messageQueueId);
	if (mq1) {
		MessageQueue *mq = new MessageQueue(mq1, 0, 0);

		StaticANIObject *stobj = g_nmi->_currentScene->getStaticANIObject1ById(_field_FA, -1);
		if (stobj) {
			stobj->_flags &= 0x100;

			ExCommand *ex = new ExCommand(stobj->_id, 34, 256, 0, 0, 0, 1, 0, 0, 0);

			ex->_z = 256;
			ex->_messageNum = 0;
			ex->_excFlags |= 3;

			mq->addExCommandToEnd(ex);
		}

		mq->setFlags(mq->getFlags() | 1);

		if (!mq->chain(0)) {
			delete mq;

			return false;
		}
	} else {
		StaticANIObject *stobj = g_nmi->_currentScene->getStaticANIObject1ById(_field_FA, -1);
		if (stobj)
			stobj->_flags &= 0xfeff;
	}

	return true;
}

bool preloadCallback(PreloadItem &pre, int flag) {
	if (flag) {
		if (flag == 50)
			g_nmi->_aniMan->preloadMovements(g_nmi->_movTable.get());

		StaticANIObject *pbar = g_nmi->_loaderScene->getStaticANIObject1ById(ANI_PBAR, -1);

		if (pbar) {
			int sz;

			if (pbar->_movement->_currMovement)
				sz = pbar->_movement->_currMovement->_dynamicPhases.size();
			else
				sz = pbar->_movement->_dynamicPhases.size();

			pbar->_movement->setDynamicPhaseIndex(flag * (sz - 1) / 100);
		}

		g_nmi->updateMap(&pre);

		g_nmi->_currentScene = g_nmi->_loaderScene;

		g_nmi->_loaderScene->draw();

		g_nmi->_system->updateScreen();
	} else {
		if (g_nmi->_scene2) {
			g_nmi->_aniMan = g_nmi->_scene2->getAniMan();
			g_nmi->_scene2 = 0;
			setInputDisabled(1);
		}

		g_nmi->_floaters->stopAll();

		if (g_nmi->_soundEnabled) {
			g_nmi->_currSoundListCount = 1;
			g_nmi->_currSoundList1[0] = g_nmi->accessScene(SC_COMMON)->_soundList.get();
		}

		g_vars->scene18_inScene18p1 = false;

		if ((pre.preloadId1 != SC_18 || pre.sceneId != SC_19) && (pre.preloadId1 != SC_19 || (pre.sceneId != SC_18 && pre.sceneId != SC_19))) {
			if (g_nmi->_scene3) {
				if (pre.preloadId1 != SC_18)
					g_nmi->_gameLoader->unloadScene(SC_18);

				g_nmi->_scene3 = 0;
			}
		} else {
			scene19_setMovements(g_nmi->accessScene(pre.preloadId1), pre.param);

			g_vars->scene18_inScene18p1 = true;

			if (pre.preloadId1 == SC_18) {
				g_nmi->_gameLoader->saveScenePicAniInfos(SC_18);

				scene18_preload();
			}
		}

		if (((pre.sceneId == SC_19 && pre.param == TrubaRight) || (pre.sceneId == SC_18 && pre.param == TrubaRight)) && !pre.preloadId2) {
			pre.sceneId = SC_18;
			pre.param = TrubaLeft;
		}

		if (!g_nmi->_loaderScene) {
			g_nmi->_gameLoader->loadScene(SC_LDR);
			g_nmi->_loaderScene = g_nmi->accessScene(SC_LDR);
		}

		StaticANIObject *pbar = g_nmi->_loaderScene->getStaticANIObject1ById(ANI_PBAR, -1);

		if (pbar) {
			pbar->show1(ST_EGTR_SLIMSORROW, ST_MAN_GOU, MV_PBAR_RUN, 0);
			pbar->startAnim(MV_PBAR_RUN, 0, -1);
		}

		g_nmi->_inventoryScene = 0;
		g_nmi->_updateCursorCallback = 0;

		g_nmi->_sceneRect.translate(-g_nmi->_sceneRect.left, -g_nmi->_sceneRect.top);

		g_nmi->_system->delayMillis(10);

		Scene *oldsc = g_nmi->_currentScene;

		g_nmi->_currentScene = g_nmi->_loaderScene;

		g_nmi->_loaderScene->draw();

		g_nmi->_system->updateScreen();

		g_nmi->_currentScene = oldsc;
	}

	return true;
}

void GameLoader::addPreloadItem(const PreloadItem &item) {
	_preloadItems.push_back(item);
}

bool GameLoader::preloadScene(int sceneId, int entranceId) {
	debugC(0, kDebugLoading, "preloadScene(%d, %d), ", sceneId, entranceId);

	if (_preloadSceneId != sceneId || _preloadEntranceId != entranceId) {
		_preloadSceneId = sceneId;
		_preloadEntranceId = entranceId;
		return true;
	}

	int idx = -1;

	for (uint i = 0; i < _preloadItems.size(); i++)
		if (_preloadItems[i].preloadId1 == sceneId && _preloadItems[i].preloadId2 == entranceId) {
			idx = i;
			break;
		}

	if (idx == -1) {
		_preloadSceneId = 0;
		_preloadEntranceId = 0;
		return false;
	}

	if (_preloadCallback) {
		if (!_preloadCallback(_preloadItems[idx], 0))
			return false;
	}

	if (g_nmi->_currentScene && g_nmi->_currentScene->_sceneId == sceneId)
		g_nmi->_currentScene = 0;

	saveScenePicAniInfos(sceneId);
	clearGlobalMessageQueueList1();
	unloadScene(sceneId);

	if (_preloadCallback)
		_preloadCallback(_preloadItems[idx], 50);

	loadScene(_preloadItems[idx].sceneId);

	ExCommand *ex = new ExCommand(_preloadItems[idx].sceneId, 17, 62, 0, 0, 0, 1, 0, 0, 0);
	ex->_excFlags = 2;
	ex->_param = _preloadItems[idx].param;

	_preloadSceneId = 0;
	_preloadEntranceId = 0;

	if (_preloadCallback)
		_preloadCallback(_preloadItems[idx], 100);

	ex->postMessage();

	return true;
}

bool GameLoader::unloadScene(int sceneId) {
	SceneTag *tag;
	int sceneTag = getSceneTagBySceneId(sceneId, &tag);

	if (sceneTag < 0)
		return false;

	if (_sc2array[sceneTag]._isLoaded)
		saveScenePicAniInfos(sceneId);

	_sc2array[sceneTag]._motionController->detachAllObjects();

	delete tag->_scene;
	tag->_scene = nullptr;

	_sc2array[sceneTag]._isLoaded = false;
	_sc2array[sceneTag]._scene = nullptr;

	return true;
}

Scene *GameLoader::accessSceneXML(int sceneId) {
	SceneTag *st;
	Sc2 *sc;

	int idx = getSceneTagBySceneId(sceneId, &st);

	if (idx < 0)
		return 0;

	sc = &_sc2array[idx];
	if (!sc->_scene) {
		if (!sc->_sceneFile.empty())
			loadSceneXML(sceneId);
		loadScene(sceneId);
	} else if (!sc->_isLoaded) {
		loadScene(sceneId);
	}
	return sc->_scene;
}

int GameLoader::getSceneTagBySceneId(int sceneId, SceneTag **st) {
	if (_sc2array.size() > 0 && _gameProject->_sceneTagList->size() > 0) {
		for (uint i = 0; i < _sc2array.size(); i++) {
			if (_sc2array[i]._sceneId == sceneId) {
				int num = 0;
				for (SceneTagList::iterator s = _gameProject->_sceneTagList->begin(); s != _gameProject->_sceneTagList->end(); ++s, num++) {
					if (s->_sceneId == sceneId) {
						*st = &(*s);
						return num;
					}
				}
			}
		}
	}

	*st = 0;
	return -1;
}

void GameLoader::applyPicAniInfos(Scene *sc, const PicAniInfoList &picAniInfo) {
	if (!picAniInfo.size())
		return;

	debugC(0, kDebugAnimation, "GameLoader::applyPicAniInfos(sc, ptr, %d)", picAniInfo.size());

	PictureObject *pict;
	StaticANIObject *ani;

	for (uint i = 0; i < picAniInfo.size(); i++) {
		const PicAniInfo &info = picAniInfo[i];
		debugC(7, kDebugAnimation, "PicAniInfo: id: %d type: %d", info.objectId, info.type);
		if (info.type & 2) {
			pict = sc->getPictureObjectById(info.objectId, info.field_8);
			if (pict) {
				pict->setPicAniInfo(info);
				continue;
			}
			pict = sc->getPictureObjectById(info.objectId, 0);
			if (pict) {
				PictureObject *pictNew = new PictureObject(pict);

				sc->_picObjList.push_back(pictNew);
				pictNew->setPicAniInfo(info);
				continue;
			}
		} else {
			if (!(info.type & 1))
				continue;

			Scene *scNew = g_nmi->accessScene(info.sceneId);
			if (!scNew)
				continue;

			ani = sc->getStaticANIObject1ById(info.objectId, info.field_8);
			if (ani) {
				ani->setPicAniInfo(picAniInfo[i]);
				continue;
			}

			ani = scNew->getStaticANIObject1ById(info.objectId, 0);
			if (ani) {
				StaticANIObject *aniNew = new StaticANIObject(ani);

				sc->addStaticANIObject(aniNew, 1);

				aniNew->setPicAniInfo(picAniInfo[i]);
				continue;
			}
		}
	}
}

void GameLoader::saveScenePicAniInfos(int sceneId) {
	SceneTag *st;

	int idx = getSceneTagBySceneId(sceneId, &st);

	if (idx < 0)
		return;

	if (!_sc2array[idx]._isLoaded)
		return;

	if (!st->_scene)
		return;

	_sc2array[idx]._picAniInfos = savePicAniInfos(st->_scene, 0, 128);
}

PicAniInfoList GameLoader::savePicAniInfos(Scene *sc, int flag1, int flag2) {
	if (!sc)
		return PicAniInfoList();

	if (!sc->_picObjList.size())
		return PicAniInfoList();

	int numInfos = sc->_staticANIObjectList1.size() + sc->_picObjList.size() - 1;
	if (numInfos < 1)
		return PicAniInfoList();

	PicAniInfoList res;
	res.reserve(numInfos);

	for (uint i = 0; i < sc->_picObjList.size(); i++) {
		PictureObject *obj = sc->_picObjList[i];

		if (obj && ((obj->_flags & flag1) == flag1) && ((obj->_field_8 & flag2) == flag2)) {
			res.push_back(PicAniInfo());
			obj->getPicAniInfo(res.back());
		}
	}

	for (uint i = 0; i < sc->_staticANIObjectList1.size(); i++) {
		StaticANIObject *obj = sc->_staticANIObjectList1[i];

		if (obj && ((obj->_flags & flag1) == flag1) && ((obj->_field_8 & flag2) == flag2)) {
			res.push_back(PicAniInfo());
			obj->getPicAniInfo(res.back());
			res.back().type &= 0xFFFF;
		}
	}

	debugC(4, kDebugBehavior | kDebugAnimation, "savePicAniInfos: Stored %d infos", res.size());

	return res;
}

void GameLoader::updateSystems(int counterdiff) {
	if (g_nmi->_currentScene) {
		g_nmi->_currentScene->update(counterdiff);

		_exCommand._messageKind = 17;
		_updateCounter++;
		_exCommand._messageNum = 33;
		_exCommand._excFlags = 0;
		_exCommand.postMessage();
	}

	processMessages();

	if (_preloadSceneId) {
		processMessages();
		preloadScene(_preloadSceneId, _preloadEntranceId);
	}
}

Sc2::Sc2() :
	_sceneId(0),
	_field_2(0),
	_scene(nullptr),
	_isLoaded(false),
	_motionController(nullptr) {}

Sc2::~Sc2() {
	delete _motionController;
}

bool Sc2::load(MfcArchive &file) {
	debugC(5, kDebugLoading, "Sc2::load()");

	_sceneId = file.readUint16LE();

	delete _motionController;
	_motionController = file.readClass<MotionController>();

	const uint count1 = file.readUint32LE();
	debugC(4, kDebugLoading, "count1: %d", count1);
	if (count1) {
		_data1.reserve(count1);
		for (uint i = 0; i < count1; i++) {
			_data1.push_back(file.readUint32LE());
		}
	}

	const uint defPicAniInfosCount = file.readUint32LE();
	debugC(4, kDebugLoading, "defPicAniInfos: %d", defPicAniInfosCount);
	if (defPicAniInfosCount) {
		_defPicAniInfos.resize(defPicAniInfosCount);
		for (uint i = 0; i < defPicAniInfosCount; i++) {
			_defPicAniInfos[i].load(file);
		}
	}

	const uint entranceDataCount = file.readUint32LE();
	debugC(4, kDebugLoading, "_entranceData: %d", entranceDataCount);

	if (entranceDataCount) {
		_entranceData.resize(entranceDataCount);
		for (uint i = 0; i < entranceDataCount; i++) {
			_entranceData[i].load(file);
		}
	}

	if (file.size() - file.pos() > 0)
		error("Sc2::load(): (%d bytes left)", file.size() - file.pos());

	return true;
}

bool PreloadItems::load(MfcArchive &file) {
	debugC(5, kDebugLoading, "PreloadItems::load()");

	int count = file.readCount();

	clear();

	resize(count);
	for (int i = 0; i < count; i++) {
		PreloadItem &t = (*this)[i];
		t.preloadId1 = file.readUint32LE();
		t.preloadId2 = file.readUint32LE();
		t.sceneId = file.readUint32LE();
		t.param = file.readSint32LE();
	}

	return true;
}

const char *getSavegameFile(int saveGameIdx) {
	static char buffer[20];
	sprintf(buffer, "fullpipe.s%02d", saveGameIdx);
	return buffer;
}

void GameLoader::restoreDefPicAniInfos() {
	for (uint i = 0; i < _sc2array.size(); i++) {
		_sc2array[i]._picAniInfos.clear();

		if (_sc2array[i]._scene)
			applyPicAniInfos(_sc2array[i]._scene, _sc2array[i]._defPicAniInfos);
	}
}

GameVar *NGIEngine::getGameLoaderGameVar() {
	if (_gameLoader)
		return _gameLoader->_gameVar;
	else
		return 0;
}

InputController *NGIEngine::getGameLoaderInputController() {
	if (_gameLoader)
		return _gameLoader->_inputController;
	else
		return 0;
}

MctlCompound *getCurrSceneSc2MotionController() {
	return getSc2MctlCompoundBySceneId(g_nmi->_currentScene->_sceneId);
}

} // End of namespace NGI
