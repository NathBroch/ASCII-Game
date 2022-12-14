#include <cmath>
#include <fstream>

#include "constants/GameConstants.h"
#include "constants/FileConstants.h"
#include "constants/InputConstants.h"
#include "constants/SettingsConstants.h"
#include "NYTimer.h"
#include "MusicNote.h"
#include "GameManager.h"

idGameManager::idGameManager(idInputManager &_input, idViewManager &_view, idSoundManager &_sound, const float _frameRate)
: input(_input)
, view(_view)
, sound(_sound)
, score()
, frameRate(_frameRate)
, timeSinceStepStart(0.0f)
, currentLevelId(0)
, nextStep(gameStep_t::LEVEL_SELECT)
, levelList()
, selectedLevelIndex(0) {
	// Register keys used in program
	for (int i = 0; i < GAME_LANE_COUNT; ++i) {
		input.RegisterKey(KeyConstants::LANE_KEYS[i]);
	}
	input.RegisterKey(KeyConstants::MENU_PREVIOUS);
	input.RegisterKey(KeyConstants::MENU_NEXT);
	input.RegisterKey(KeyConstants::MENU_CONFIRM);
	input.RegisterKey(KeyConstants::APPLICATION_EXIT);

	// Load data about levels
	if (!LoadLevelsData()) {
		nextStep = gameStep_t::QUIT_ERROR;
	}
}

bool idGameManager::LoadLevelsData() {
	// Load level list
	std::ifstream file(PathConstants::GameData::LEVEL_LIST);
	if (!file.good() || !file.is_open()) {
		return false;
	}
	
	std::string levelFileName;
	std::string levelDisplayName;
	std::pair<std::string, std::string> levelListElement;
	while (!file.eof()) {
		file >> levelFileName;
		if (file.fail()) {
			break; // Fail at file name retrieval, we assume it's the end of file
		}
		file >> std::ws;
		std::getline(file, levelDisplayName);
		if (file.fail()) {
			return false; // Fail at display name retrieval, the file is invalid
		}
		levelListElement.first = levelFileName;
		levelListElement.second = levelDisplayName;
		levelList.push_back(levelListElement);
	}

	// Load high score list
	score.LoadHighScores(PathConstants::GameData::LEVEL_HIGH_SCORES);

	return true;
}

int idGameManager::StartMainLoop() {
	std::function<bool(void)> stepInitFunc = NULL;
	std::function<bool(void)> stepUpdateFunc = NULL;

	while ((nextStep != gameStep_t::QUIT_SUCCESS) && (nextStep != gameStep_t::QUIT_ERROR)) {
		// Setup Init and Update function according to next step
		switch (nextStep) {
			case gameStep_t::LEVEL_SELECT:
				stepInitFunc = std::bind(&idGameManager::SelectLevelInit, this);
				stepUpdateFunc = std::bind(&idGameManager::SelectLevelUpdate, this);
				break;
			case gameStep_t::LEVEL_PLAY:
				stepInitFunc = std::bind(&idGameManager::PlayLevelInit, this);
				stepUpdateFunc = std::bind(&idGameManager::PlayLevelUpdate, this);
				break;
			case gameStep_t::LEVEL_RESULTS:
				stepInitFunc = std::bind(&idGameManager::LevelResultsInit, this);
				stepUpdateFunc = std::bind(&idGameManager::LevelResultsUpdate, this);
				break;
			default:
				stepInitFunc = NULL;
				stepUpdateFunc = NULL;
				break;
		}
		// Play next game step
		PlayGameStep(stepInitFunc, stepUpdateFunc);
	}

	if (nextStep == gameStep_t::QUIT_SUCCESS) {
		return EXIT_SUCCESS;
	} else {
		return EXIT_FAILURE;
	}
}

void idGameManager::PlayGameStep(std::function<bool(void)> stepInitFunc, std::function<bool(void)> stepUpdateFunc) {
	// If no function given, skip step
	if ((stepInitFunc == NULL) && (stepUpdateFunc == NULL)) {
		return;
	}

	if (stepInitFunc != NULL) {
		// If init function failed, stop step
		if (!stepInitFunc()) {
			nextStep = gameStep_t::QUIT_ERROR;
			return;
		}
	}

	// If no function given, skip step
	if (stepUpdateFunc == NULL) {
		return;
	}
	
	// Prepare Update Loop
	const float delayBetweenFrames = 1.0f / frameRate;
	timeSinceStepStart = 0.0f;
	bool shouldStop = stepUpdateFunc();

	float startTime = timer.getElapsedSeconds();
	float previousUpdateTime = startTime;
	float currentLoopTime;

	// Play Update Loop
	while (!shouldStop) {
		input.UpdateKeyStates();
		currentLoopTime = timer.getElapsedSeconds();

		if (currentLoopTime > (previousUpdateTime + delayBetweenFrames)) {
			timeSinceStepStart = currentLoopTime - startTime;

			shouldStop = stepUpdateFunc();
			sound.UpdateSourceStates();
			input.ResetKeyStates();

			previousUpdateTime = currentLoopTime;
		}
		Sleep(1);
	}
}

bool idGameManager::SelectLevelInit() {
	// Load menu sound effects
	if (!sound.LoadWav(PathConstants::Audio::Effects::MENU_NAVIGATE)) {
		nextStep = gameStep_t::QUIT_ERROR;
		return false;
	}
	if (!sound.LoadWav(PathConstants::Audio::Effects::MENU_CONFIRM)) {
		nextStep = gameStep_t::QUIT_ERROR;
		return false;
	}

	// Init UI
	view.ClearConsole();
	view.ClearUI();
	view.DrawUIBorder();

	std::string songNames[MAX_LEVEL_COUNT];
	for (size_t i = 0; i < levelList.size(); i++){
		songNames[i] = levelList[i].second;
	}
	view.DrawSelectUI(songNames, levelList.size());
	view.UpdateSelectUI(
		selectedLevelIndex,
		score.GetHighScore(levelList[selectedLevelIndex].first)
	);
	view.Refresh();

	return true;
}

bool idGameManager::SelectLevelUpdate() {
	// # Handle quitting the application
	if (input.WasKeyPressed(KeyConstants::APPLICATION_EXIT)) {
		view.ClearConsole();
		view.Refresh();
		nextStep = gameStep_t::QUIT_SUCCESS;
		return true;
	}

	// # Menu navigation
	const size_t levelCount = levelList.size();

	bool selectionChanged = false;
	if (input.WasKeyPressed(KeyConstants::MENU_NEXT)) {
		selectedLevelIndex = (selectedLevelIndex + 1) % levelCount;
		selectionChanged = true;
	}
	if (input.WasKeyPressed(KeyConstants::MENU_PREVIOUS)) {
		selectedLevelIndex = (selectedLevelIndex + levelCount - 1) % levelCount;
		selectionChanged = true;
	}
	bool selectionConfirmed = input.WasKeyPressed(KeyConstants::MENU_CONFIRM);

	// # Sound playing
	if (selectionChanged) {
		if (!sound.Play(PathConstants::Audio::Effects::MENU_NAVIGATE)) {
			nextStep = gameStep_t::QUIT_ERROR;
			return true;
		}
	}
	if (selectionConfirmed) {
		if (!sound.Play(PathConstants::Audio::Effects::MENU_CONFIRM)) {
			nextStep = gameStep_t::QUIT_ERROR;
			return true;
		}
	}
	
	// # UI Display
	if (selectionConfirmed) {
		nextStep = gameStep_t::LEVEL_PLAY;
		view.DrawConfirmedUI(selectedLevelIndex);
		view.Refresh();
		Sleep(1000);
		return true;
	} else {
		if (selectionChanged) {
			view.UpdateSelectUI(
				selectedLevelIndex,
				score.GetHighScore(levelList[selectedLevelIndex].first)
			);
			view.Refresh();
		}
	}

	return false;
}

bool idGameManager::PlayLevelInit() {
	// Load sound effects
	if (!sound.LoadWav(PathConstants::Audio::Effects::COMBO_BREAK)) {
		nextStep = gameStep_t::QUIT_ERROR;
		return false;
	}

	// Load level
	std::string levelFileName = PathConstants::GameData::LEVELS_DIR;
	levelFileName.append(levelList[selectedLevelIndex].first);

	if (!currentLevel.LoadFile(levelFileName)) {
		nextStep = gameStep_t::QUIT_ERROR;
		return false;
	}

	// Load level music data and play it
	std::string songFilePath = PathConstants::Audio::SONGS_DIR;
	songFilePath.append(currentLevel.GetAudioFileName());

	if (!sound.LoadWav(songFilePath)) {
		nextStep = gameStep_t::QUIT_ERROR;
		return false;
	}

	if (!sound.Play(songFilePath)) {
		nextStep = gameStep_t::QUIT_ERROR;
		return false;
	}

	// Reset score data
	score.Reset();
	for (int i = 0; i < GAME_LANE_COUNT; ++i) {
		latestLaneMistakes[i] = -2 * GameplaySettingsConstants::NOTE_ERROR_DISPLAY_DURATION;
	}

	// Draw UI
	const float songLength = currentLevel.GetLengthSeconds();
	view.ClearUI();
	view.DrawUI(currentLevel.GetSongName(), int(songLength));
	
	return true;
}

bool idGameManager::PlayLevelUpdate() {
	if (!UpdateGameData() || !UpdateGameView()) {
		nextStep = gameStep_t::QUIT_ERROR;
		return true;
	}

	if (timeSinceStepStart <= currentLevel.GetLengthSeconds()) {
		return false;
	} else {
		nextStep = gameStep_t::LEVEL_RESULTS;
		return true;
	}
}

bool idGameManager::UpdateGameData() {
	// # Input Management
	currentLevel.ActivateNotesForTime(timeSinceStepStart);

	const float pressEarlyTolerance = GameplaySettingsConstants::EARLY_PRESS_TOLERANCE_SECONDS;
	const float pressLateTolerance = GameplaySettingsConstants::LATE_PRESS_TOLERANCE_SECONDS;
	const float releaseEarlyTolerance = GameplaySettingsConstants::EARLY_RELEASE_TOLERANCE_SECONDS;
	const float maxMissTimeDistance = GameplaySettingsConstants::MAX_MISS_TIME_DISTANCE_SECONDS;

	bool isBigComboLoss = false;
	for (int i = 0; i < GAME_LANE_COUNT; ++i)
	{
		// Get active notes for current lane
		std::deque<idMusicNote> &laneNotes = currentLevel.GetEditableActiveNotes(i);
		size_t laneNotesCount = laneNotes.size();
		if (laneNotesCount <= 0) {
			continue;
		}

		// Retrieve the "lowest" (closest to the bottom) note that wasn't already played by the player
		int bottomNoteIndex = 0;
		idMusicNote* bottomNote;
		do {
			bottomNote = &laneNotes[bottomNoteIndex++];
		} while (
			(bottomNoteIndex < laneNotesCount) &&
			(bottomNote->state != idMusicNote::state_t::ACTIVE) && 
			(timeSinceStepStart > bottomNote->endSeconds - releaseEarlyTolerance));

		// Update note state
		if (bottomNote->state != idMusicNote::state_t::MISSED) {
			if (bottomNote->state == idMusicNote::state_t::ACTIVE) {
				if (timeSinceStepStart > bottomNote->startSeconds + pressLateTolerance) {
					bottomNote->state = idMusicNote::state_t::MISSED;
					isBigComboLoss |= RegisterMissOnLane(i);
				} else if (input.WasKeyPressed(KeyConstants::LANE_KEYS[i])) {
					if (timeSinceStepStart >= bottomNote->startSeconds - pressEarlyTolerance) {
						bottomNote->state = idMusicNote::state_t::PRESSED;
					}
					else if (timeSinceStepStart + maxMissTimeDistance >= bottomNote->startSeconds - pressEarlyTolerance) {
						bottomNote->state = idMusicNote::state_t::MISSED;
						isBigComboLoss |= RegisterMissOnLane(i);
					}
				}
			} else if (bottomNote->state == idMusicNote::state_t::PRESSED) {
				if (input.WasKeyReleased(KeyConstants::LANE_KEYS[i]) &&
					(timeSinceStepStart <= bottomNote->endSeconds - releaseEarlyTolerance)) {
					bottomNote->state = idMusicNote::state_t::MISSED;
					isBigComboLoss |= RegisterMissOnLane(i);
				}
			}
		}
	}
	currentLevel.RemoveNotesForTime(timeSinceStepStart, pressLateTolerance);

	// # Score Management
	const std::vector<idMusicNote> &playedNotes = currentLevel.GetPlayedNotes();
	for (int i = 0; i < playedNotes.size(); ++i) {
		const unsigned int comboCountBeforeNote = score.GetComboCount();
		const idMusicNote &note = playedNotes[i];
		
		if (note.state == idMusicNote::state_t::PRESSED) {
			score.RegisterHit((note.endSeconds - note.startSeconds) * 10.0f);
		} else if (note.state != idMusicNote::state_t::MISSED) { // Missed notes are already registered
			isBigComboLoss |= RegisterMissOnLane(i);
		}
	}
	currentLevel.ClearPlayedNotes();
	
	if (isBigComboLoss && !sound.Play(PathConstants::Audio::Effects::COMBO_BREAK)) {
		return false;
	}

	return true;
}

bool idGameManager::RegisterMissOnLane(const int lane) {
	const unsigned int comboCountBeforeNote = score.GetComboCount();

	score.RegisterMiss();
	latestLaneMistakes[lane] = timeSinceStepStart;

	return (comboCountBeforeNote >= GameplaySettingsConstants::BIG_COMBO_LOSS_THRESHOLD);
}

bool idGameManager::UpdateGameView() {
	// Draw notes
	view.ClearNotesArea();
	const float &laneLengthSeconds = currentLevel.GetLaneLengthSeconds();
	for (int lane = 0; lane < GAME_LANE_COUNT; ++lane) {
		const std::deque<idMusicNote> &laneNotes = currentLevel.GetReadonlyActiveNotes(lane);
		for (int i = 0; i < laneNotes.size(); ++i) {
			const idMusicNote &note = laneNotes[i];
			view.DrawNote(note, laneLengthSeconds, timeSinceStepStart);
		}
	}
	
	// Draw bottom bar
	bool heldKeys[GAME_LANE_COUNT];
	bool laneHasRecentMistake[GAME_LANE_COUNT];
	for (int i = 0; i < GAME_LANE_COUNT; ++i) {
		heldKeys[i] = input.WasKeyHeld(KeyConstants::LANE_KEYS[i]);
		laneHasRecentMistake[i] = 
			((timeSinceStepStart - latestLaneMistakes[i]) <= GameplaySettingsConstants::NOTE_ERROR_DISPLAY_DURATION);
	}
	view.DrawBottomBar(heldKeys, laneHasRecentMistake);

	// Draw UI
	view.UpdateUI(
		int(timeSinceStepStart),
		score.GetScore(),
		score.GetComboCount(),
		score.IsFullCombo(),
		score.GetMissedNotesCount(),
		score.GetHighScore(levelList[selectedLevelIndex].first),
		score.IsHighScore(levelList[selectedLevelIndex].first)
	);
	
	view.Refresh();

	return true;
}

bool idGameManager::LevelResultsInit() {
	// Unload level music
	std::string audioFilePath = PathConstants::Audio::SONGS_DIR;
	audioFilePath.append(currentLevel.GetAudioFileName());
	if (!sound.UnloadFile(audioFilePath)) {
		nextStep = gameStep_t::QUIT_ERROR;
		return false;
	}

	// Load sound effect
	if (!sound.LoadWav(PathConstants::Audio::Effects::MENU_BACK)) {
		nextStep = gameStep_t::QUIT_ERROR;
		return false;
	}

	// Draw results
	view.ClearNotesArea();
	view.ClearUIBottom();
	view.DrawResults(
		score.GetScore(), 
		score.IsHighScore(levelList[selectedLevelIndex].first), 
		score.GetAccuracy(), 
		score.GetPlayedNotesCount() - score.GetMissedNotesCount(),
		score.GetPlayedNotesCount(),
		score.GetMaxComboCount(), 
		score.GetMissedNotesCount());
	view.Refresh();

	// Update high score file if needed
	if (score.IsHighScore(levelList[selectedLevelIndex].first)) {
		score.UpdateHighScore(levelList[selectedLevelIndex].first);
		if (!score.SaveHighScores(PathConstants::GameData::LEVEL_HIGH_SCORES)) {
			return false;
		}
	}

	return true;
}

bool idGameManager::LevelResultsUpdate() {
	view.UpdateResults(int(timeSinceStepStart) % 2);
	view.Refresh();

	// Check for input
	if (input.WasKeyPressed(KeyConstants::MENU_CONFIRM)) {
		// Play sound effect
		if (!sound.Play(PathConstants::Audio::Effects::MENU_BACK)) {
			nextStep = gameStep_t::QUIT_ERROR;
			return false;
		}

		nextStep = gameStep_t::LEVEL_SELECT;
		return true;
	}
	
	return false;
}
