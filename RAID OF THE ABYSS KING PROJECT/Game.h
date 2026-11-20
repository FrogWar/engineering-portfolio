#ifndef GAME_H
#define GAME_H

#include "Boss.h"
#include "Player.h"
#include <SFML/Graphics.hpp>
#include <memory>
#include <string>
#include <vector>

struct Button {
    sf::RectangleShape shape;
    sf::Text label;
    std::string action;

    Button() = default;
    Button(const sf::Font& font, const std::string& text, const std::string& actionName,
           sf::Vector2f position, sf::Vector2f size);
};

class Game {
private:
    sf::RenderWindow window;
    sf::Font font;
    bool fontLoaded;

    std::vector<std::unique_ptr<Player>> players;
    Boss boss;
    int currentPlayerIndex;
    int selectedPlayerIndex;
    bool gameOver;
    bool playerWon;
    bool turnInProgress; //for power transfering
    bool targetingMode; //power transfer

    std::vector<Button> actionButtons;
    std::vector<std::string> battleLog;

    void loadResources();
    void buildButtons();
    void processEvents();
    void handleLeftClick(sf::Vector2i mousePos);
    void performAction(const std::string& action);
    void endTurnIfNeeded();
    void bossTurn();
    int  findNextAlivePlayer(int startIndex) const;
    bool allPlayersDefeated() const;
    void resetGame();
    void addLog(const std::string& line);
    void executeSpecialMove(); 


    void draw();
    void drawBackground();
    void drawTitle();
    void drawPlayers();
    void drawBoss();
    void drawButtons();
    void drawLog();
    void drawStatusBar();
    void drawCenteredText(const std::string& text, unsigned int size, sf::Vector2f center, sf::Color color);
    bool pointInRect(sf::Vector2f point, const sf::FloatRect& rect) const;

public:
    Game();
    void run();
};

#endif
