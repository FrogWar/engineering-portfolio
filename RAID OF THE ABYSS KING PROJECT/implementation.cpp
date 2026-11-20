#include "Game.h"
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <optional>
#include <sstream>
#include <stdexcept>

// =====================================================
// Character
// =====================================================

Character::Character(const std::string& n, int hp, int atk, int en)
    : name(n), health(hp), maxHealth(hp), energy(en), attackPower(atk), defending(false) {
    if (hp <= 0 || atk <= 0 || en < 0) {
        throw std::invalid_argument("Invalid character stats.");
    }
}

void Character::takeDamage(int amount) {
    if (amount < 0) {
        throw std::invalid_argument("Damage cannot be negative.");
    }
    if (defending) {
        amount /= 2;
        defending = false;
    }
    health -= amount;
    if (health < 0) health = 0;
}

void Character::heal(int amount) {
    if (amount < 0) {
        throw std::invalid_argument("Heal amount cannot be negative.");
    }
    health += amount;
    if (health > maxHealth) health = maxHealth;
}

bool Character::isAlive() const { return health > 0; }
void Character::setDefending(bool value) { defending = value; }
bool Character::isDefending() const { return defending; }
const std::string& Character::getName() const { return name; }
int Character::getHealth() const { return health; }
int Character::getMaxHealth() const { return maxHealth; }
int Character::getEnergy() const { return energy; }
int Character::getAttackPower() const { return attackPower; }

void Character::addEnergy(int amount) {
    energy += amount;
    if (energy > 100) energy = 100;
}

void Character::consumeEnergy(int amount) {
    if (energy < amount) throw std::runtime_error("Not enough energy.");
    energy -= amount;
}

// =====================================================
// Player
// =====================================================

Player::Player(const std::string& n, int hp, int atk, int en, int pots)
    : Character(n, hp, atk, en), potions(pots) {}

int Player::usePotion() {
    if (potions <= 0) throw std::runtime_error("No potions remaining.");
    potions--;
    heal(90);
    return 90;
}

int Player::getPotions() const { return potions; }
void Player::addPotion(int amount) { potions += amount; }

Player& Player::operator+=(int amount) {
    heal(amount);
    return *this;
}

bool Player::operator==(const Player& other) const {
    return name == other.name && getClassName() == other.getClassName();
}

std::ostream& operator<<(std::ostream& os, const Player& p) {
    os << p.getClassName() << "(" << p.getName() << ") HP: " << p.getHealth();
    return os;
}

// =====================================================
// Warrior
// =====================================================

Warrior::Warrior(const std::string& n) : Player(n, 150, 30, 90, 2) {}

int Warrior::basicAttack(Character& target) {
    int damage = attackPower + (std::rand() % 10);
    target.takeDamage(damage);
    addEnergy(10);
    return damage;
}

int Warrior::specialMove(Character& target) {
    consumeEnergy(30);
    int damage = attackPower * 2 + (std::rand() % 15);
    target.takeDamage(damage);
    return damage;
}

std::string Warrior::getClassName() const { return "Warrior"; }

// =====================================================
// Mage
// =====================================================

Mage::Mage(const std::string& n) : Player(n, 110, 23, 120, 3) {}

int Mage::basicAttack(Character& target) {
    int damage = attackPower + (std::rand() % 12);
    target.takeDamage(damage);
    addEnergy(10);
    return damage;
}

// int Mage::specialMove(Character& target) {
//     consumeEnergy(30);
//     int damage = attackPower * 3 + (std::rand() % 20);
//     target.takeDamage(damage);
//     return damage;
// }

int Mage::specialMove(Character& target) {
    // energy cost for the Mage to transfer energy
    consumeEnergy(30); 
    
    // Generate a random energy gift: 30-45 EN
    int amount = 30 + (std::rand() % 15); 
    target.addEnergy(amount);
    
    return amount;
}

std::string Mage::getClassName() const { return "Mage"; }

// =====================================================
// Healer
// =====================================================

Healer::Healer(const std::string& n) : Player(n, 125, 16, 100, 4) {}

int Healer::basicAttack(Character& target) {
    int damage = attackPower + (std::rand() % 8);
    target.takeDamage(damage);
    addEnergy(10);
    return damage;
}

// int Healer::specialMove(Character& target) {
//     (void)target;
//     consumeEnergy(60);
//     int healAmount = 60 + (std::rand() % 20);
//     heal(healAmount);
//     return healAmount;
// }

int Healer::specialMove(Character& target) {
    consumeEnergy(30); // Make revives expensive!
    
    int amount = 60 + (std::rand() % 20);
    target.heal(amount); 
    
    return amount;
}

std::string Healer::getClassName() const { return "Healer"; }

// =====================================================
// Boss
// =====================================================

Boss::Boss(const std::string& n) : Character(n, 420, 15, 100), rage(0) {}

int Boss::basicAttack(Character& target) {
    // Adds 1 damage for every 10 points of rage
    int rageBonus = rage / 10; 
    int damage = attackPower + rageBonus + (std::rand() % 15);
    
    target.takeDamage(damage);
    addRage(10);
    return damage;
}

int Boss::specialMove(Character& target) {
    // Special moves now deal 2.5x base damage instead of 2.0x
    int damage = static_cast<int>(attackPower * 1.5) + (std::rand() % 25);
    
    // If rage is high, deal an extra "execute" bonus
    if (rage >= 60) {
        damage += 9;
    }

    target.takeDamage(damage);
    addRage(15);
    return damage;
}

std::string Boss::getClassName() const { return "Boss"; }
int Boss::getRage() const { return rage; }

void Boss::addRage(int amount) {
    rage += amount;
    if (rage > 100) rage = 100;
}

std::string Boss::chooseAttackName() const {
    if (rage >= 75) return "Crimson Fury";
    if (rage >= 50) return "Dark Glamour";
    if (rage >= 25) return "Shadow Kiss";
    return "Cursed Glare";
}

// =====================================================
// Dark & Crimson Color Palette
// =====================================================

namespace Theme {
    const sf::Color WindowBg       (14, 3, 4);      // Near-black abyssal red
    const sf::Color PanelBg        (26, 5, 7);      // Dark crimson panel fill
    const sf::Color SubPanelBg     (20, 4, 5);      // Darker inset panels (Boss / Log)
    const sf::Color CardBg         (34, 7, 9);      // Unselected hero card
    const sf::Color CardSelectedBg (58, 12, 15);    // Selected hero card

    const sf::Color BorderRed      (190, 28, 28);   // Primary scarlet borders
    const sf::Color BorderDim      (135, 22, 22);   // Subtle button/panel borders
    const sf::Color BorderGold     (255, 200, 40);  // Active turn highlight
    const sf::Color DeadMuted      (65, 28, 28);    // Defeated hero/boss tint

    const sf::Color TitleRed       (225, 45, 45);   // Main title & boss name
    const sf::Color TextLight      (242, 225, 225); // High-contrast off-white text
    const sf::Color TextSub        (215, 135, 135); // Muted rose-red labels/subtitles

    const sf::Color HpBarBg        (55, 12, 12);    // Empty health bar track
    const sf::Color HpBarFill      (220, 24, 24);   // Bright blood-red health fill

    const sf::Color ButtonFill     (48, 9, 11);     // Dark crimson button fill
    const sf::Color ButtonOutline  (185, 30, 30);   // Scarlet button border
}

// =====================================================
// Button
// =====================================================

Button::Button(const sf::Font& font, const std::string& text, const std::string& actionName,
               sf::Vector2f position, sf::Vector2f size)
    : shape(size), label(font), action(actionName) {
    shape.setPosition(position);
    shape.setFillColor(Theme::ButtonFill);
    shape.setOutlineThickness(2.f);
    shape.setOutlineColor(Theme::ButtonOutline);

    label.setString(text);
    label.setCharacterSize(20);
    label.setFillColor(Theme::TextLight);

    sf::FloatRect bounds = label.getLocalBounds();
    label.setPosition({
        position.x + (size.x - bounds.size.x) / 2.f - bounds.position.x,
        position.y + (size.y - bounds.size.y) / 2.f - bounds.position.y
    });
}

// =====================================================
// Game
// =====================================================

Game::Game()
    : window(sf::VideoMode({1200u, 760u}), "Queen of the Abyss"),
      fontLoaded(false),
      boss("Queen Malefira"),
      currentPlayerIndex(0),
      selectedPlayerIndex(0),
      gameOver(false),
      turnInProgress(false),
      targetingMode(false),
      playerWon(false) {
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    window.setFramerateLimit(60);

    players.emplace_back(std::make_unique<Warrior>("Rosalind"));
    players.emplace_back(std::make_unique<Mage>("Stellara"));
    players.emplace_back(std::make_unique<Healer>("Seraphine"));

    loadResources();
    buildButtons();
    addLog("Finally, the battle begins! Draw your weapons!");
}

void Game::loadResources() {
    const std::vector<std::string> candidates = {
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/verdana.ttf",
        "C:/Windows/Fonts/calibri.ttf"
    };

    for (const auto& path : candidates) {
        if (font.openFromFile(path)) {
            fontLoaded = true;
            return;
        }
    }

    throw std::runtime_error("Could not load a font. Update loadResources() with a valid font path on your machine.");
}

void Game::buildButtons() {
    actionButtons.clear();
    actionButtons.emplace_back(font, "Attack", "attack", sf::Vector2f{40.f, 620.f}, sf::Vector2f{140.f, 46.f});
    actionButtons.emplace_back(font, "Special", "special", sf::Vector2f{195.f, 620.f}, sf::Vector2f{140.f, 46.f});
    actionButtons.emplace_back(font, "Defend", "defend", sf::Vector2f{350.f, 620.f}, sf::Vector2f{140.f, 46.f});
    actionButtons.emplace_back(font, "Use Item", "item", sf::Vector2f{505.f, 620.f}, sf::Vector2f{140.f, 46.f});
    actionButtons.emplace_back(font, "Restart", "restart", sf::Vector2f{660.f, 620.f}, sf::Vector2f{140.f, 46.f});
}

void Game::run() {
    while (window.isOpen()) {
        processEvents();
        draw();
    }
}

void Game::processEvents() {
    while (const std::optional event = window.pollEvent()) {
        if (event->is<sf::Event::Closed>()) {
            window.close();
        } else if (const auto* keyPressed = event->getIf<sf::Event::KeyPressed>()) {
            if (keyPressed->scancode == sf::Keyboard::Scancode::Escape) {
                window.close();
            }
        } else if (const auto* mousePressed = event->getIf<sf::Event::MouseButtonPressed>()) {
            if (mousePressed->button == sf::Mouse::Button::Left) {
                handleLeftClick(mousePressed->position);
            }
        }
    }
}

void Game::handleLeftClick(sf::Vector2i mousePos) {
    const sf::Vector2f point{static_cast<float>(mousePos.x), static_cast<float>(mousePos.y)};

    for (std::size_t i = 0; i < players.size(); ++i) {
        // MATCH the x coordinate: Change 30.f to 60.f
        const float x = 60.f + i * 180.f;
        // MATCH the height: Change 220.f to 250.f
        sf::FloatRect cardRect({x, 120.f}, {160.f, 250.f});
        // 1. First, check if the click was even inside the card area
        if (pointInRect(point, cardRect)) {
            
            if (!targetingMode) {
                // NORMAL SELECTION: To ACT, the hero must be alive
                if (players[i]->isAlive()) {
                    currentPlayerIndex = static_cast<int>(i);
                    selectedPlayerIndex = static_cast<int>(i);
                    addLog(players[i]->getName() + "  Preparing to strike!");
                } else {
                    addLog(players[i]->getName() + " is Unconcisous! Select someone else!");
                }
            } else {
                // TARGETING MODE: We don't care if they are alive! 
                // This allows you to click a dead hero to revive them.
                selectedPlayerIndex = static_cast<int>(i); 
                executeSpecialMove(); 
                targetingMode = false; 
            }
            return;
        }
    }

    for (const auto& button : actionButtons) {
        if (pointInRect(point, button.shape.getGlobalBounds())) {
            performAction(button.action);
            return;
        }
    }
}


void Game::performAction(const std::string& action) {
    if (action == "restart") {
        resetGame();
        return;
    }

    if (gameOver) {
        addLog("Game over!");
        return;
    }

    // Removing the sequential player select feature
    // if (selectedPlayerIndex != currentPlayerIndex) {
    //     addLog("It's " + players[currentPlayerIndex]->getName() + "'s turn!");
    //     return;
    // }

    Player& player = *players[currentPlayerIndex];
    if (!player.isAlive()) {
        endTurnIfNeeded();
        return;
    }

    try {
        if (action == "attack") {
            int dmg = player.basicAttack(boss);
            addLog(player.getName() + " strikes for " + std::to_string(dmg) + " damage!");
        } else if (action == "special") {
    if (player.getClassName() == "Warrior") {
        // Warrior targets the boss immediately - no teammate selection needed
        int dmg = player.specialMove(boss);
        addLog(player.getName() + " unleashed her fury for " + std::to_string(dmg) + " damage!");
        endTurnIfNeeded(); // Turn ends immediately
    } else {
        // Mage and Healer enter Targeting Mode
        targetingMode = true; 
        addLog("Select a teammate for " + player.getName() + "'s special move!");
        // dont call endTurnIfNeeded() here yet!
    }
}
     else if (action == "defend") {
            player.setDefending(true);
            player.addEnergy(10);
            addLog(player.getName() + " Is protecting the team!");
        } else if (action == "item") {
            int amount = player.usePotion();
            addLog(player.getName() + " drinks a potion and restores " + std::to_string(amount) + " HP!");
        }
    } catch (const std::exception& ex) {
        addLog(std::string("Oops! ") + ex.what());
        return;
    }

    if (!boss.isAlive()) {
        gameOver = true;
        playerWon = true;
        addLog("Queen Malefira has fallen!");
        return;
    }

    if (!targetingMode) {
        endTurnIfNeeded();
    }
}

void Game::executeSpecialMove() {
    Player& player = *players[currentPlayerIndex];
    Player& target = *players[selectedPlayerIndex];

    try {
        if (player.getClassName() == "Healer") {
            int amount = player.specialMove(target);
            addLog(player.getName() + " revived/healed " + target.getName() + " for " + std::to_string(amount) + " HP!");
        } else if (player.getClassName() == "Mage") {
            if (&player == &target) {
                addLog("The Mage can't give energy to herself!");
                targetingMode = true; // Stay in mode to pick a valid target
                return;
            }
            int amount = player.specialMove(target);
            addLog(player.getName() + " granted " + std::to_string(amount) + " Energy to " + target.getName() + "!");
        }
        
        endTurnIfNeeded(); // Boss counter-attacks after the move is finished
    } catch (const std::exception& ex) {
        addLog(std::string("Oops! ") + ex.what());
    }
}

// void Game::endTurnIfNeeded() {
//     int next = findNextAlivePlayer(currentPlayerIndex + 1);
//     currentPlayerIndex = next;
//     selectedPlayerIndex = next;

//     if (next == 0) {
//         bossTurn();
//         if (allPlayersDefeated()) {
//             gameOver = true;
//             playerWon = false;
//             addLog("Kneel in the dirt, or fall in it. The result is the same.");
//             return;
//         }
//         currentPlayerIndex = findNextAlivePlayer(0);
//         selectedPlayerIndex = currentPlayerIndex;
//     }
// }

// void Game::endTurnIfNeeded() {
//     // 1. Find who should go next
//     int next = findNextAlivePlayer(currentPlayerIndex + 1);

//     // 2. If 'next' is smaller than or equal to current, a full round has passed
//     if (next <= currentPlayerIndex) {
//         bossTurn();
        
//         // 3. Check if the boss just finished everyone off
//         if (allPlayersDefeated()) {
//             gameOver = true;
//             playerWon = false;
//             addLog("Kneel in the dirt, or fall in it. The result is the same.");
//             return;
//         }
        
//         // 4. Refresh 'next' in case the boss killed the person who was supposed to go
//         next = findNextAlivePlayer(0);
//     }

//     // 5. Update the game state
//     currentPlayerIndex = next;
//     selectedPlayerIndex = next;
// }

void Game::endTurnIfNeeded() {
    // 1. The Boss strikes back immediately after the active hero acts
    bossTurn();

    // 2. Check if the team was defeated by that counter-attack
    if (allPlayersDefeated()) {
        gameOver = true;
        playerWon = false;
        addLog("Queen Malefira: Kneel in the dirt, or fall in it. The result is the same.");
        return;
    }
    turnInProgress = false;

    // 3. Move to the next living hero in the rotation
    // int next = findNextAlivePlayer(currentPlayerIndex + 1);
    
    // // 4. Update the game state so the GUI highlights the new active hero
    // currentPlayerIndex = next;
    // selectedPlayerIndex = next;
}

void Game::bossTurn() {
    std::vector<int> aliveIndices;
    for (int i = 0; i < static_cast<int>(players.size()); ++i) {
        if (players[i]->isAlive()) aliveIndices.push_back(i);
    }
    if (aliveIndices.empty()) return;

    int targetIdx = aliveIndices[std::rand() % aliveIndices.size()];
    Player& target = *players[targetIdx];

    int dmg = 0;
    if (boss.getRage() >= 50) {
        dmg = boss.specialMove(target);
        addLog("Queen Malefira uses " + boss.chooseAttackName() + " on " + target.getName() + " for " + std::to_string(dmg) + " damage!");
    } else {
        dmg = boss.basicAttack(target);
        addLog("Queen Malefira attacks " + target.getName() + " for " + std::to_string(dmg) + " damage!");
    }
}

int Game::findNextAlivePlayer(int startIndex) const {
    const int n = static_cast<int>(players.size());
    for (int i = 0; i < n; ++i) {
        const int idx = (startIndex + i) % n;
        if (players[idx]->isAlive()) {
            return idx;
        }
    }
    return 0;
}

bool Game::allPlayersDefeated() const {
    for (const auto& player : players) {
        if (player->isAlive()) return false;
    }
    return true;
}

void Game::resetGame() {
    players.clear();
    players.emplace_back(std::make_unique<Warrior>("Rosalind"));
    players.emplace_back(std::make_unique<Mage>("Stellara"));
    players.emplace_back(std::make_unique<Healer>("Seraphine"));
    boss = Boss("Queen Malefira");
    currentPlayerIndex = 0;
    selectedPlayerIndex = 0;
    gameOver = false;
    playerWon = false;
    battleLog.clear();
    addLog("We found her! SHOW NO MERCY!");
}

void Game::addLog(const std::string& line) {
    battleLog.push_back(line);
    if (battleLog.size() > 7) {
        battleLog.erase(battleLog.begin());
    }
}

void Game::draw() {
    window.clear(Theme::WindowBg);
    drawBackground();
    drawPlayers();
    drawBoss();
    drawTitle();
    drawButtons();
    drawLog();
    drawStatusBar();
    window.display();
}

void Game::drawBackground() {
    sf::RectangleShape panel({1140.f, 700.f});
    panel.setPosition({30.f, 30.f});
    panel.setFillColor(Theme::PanelBg);
    panel.setOutlineColor(Theme::BorderRed);
    panel.setOutlineThickness(3.f);
    window.draw(panel);
}

void Game::drawTitle() {
    drawCenteredText("Queen of the Abyss", 34, {600.f, 60.f}, Theme::TitleRed);

    if (gameOver) {
        if (playerWon) {
            drawCenteredText("VICTORY!", 28, {600.f, 95.f}, Theme::BorderGold);
        } else {
            drawCenteredText("Her victory. Your extinction.", 28, {600.f, 95.f}, sf::Color(255, 30, 30));
        }
    } else {
        drawCenteredText("Turn: " + players[currentPlayerIndex]->getName(), 22, {600.f, 95.f}, Theme::TextLight);
    }
}

void Game::drawPlayers() {
    // Distinct dark-red / burgundy shades for Warrior, Mage, and Healer
    const std::vector<sf::Color> colors = {
        sf::Color(195, 30, 30),  // Warrior: Scarlet
        sf::Color(115, 25, 55),  // Mage: Deep Burgundy
        sf::Color(155, 22, 38)   // Healer: Ruby Crimson
    };

    for (std::size_t i = 0; i < players.size(); ++i) {
        const float x = 60.f + i * 180.f;
        const float y = 120.f;

        sf::RectangleShape card({160.f, 250.f});
        card.setPosition({x, y});
        card.setFillColor(i == static_cast<std::size_t>(selectedPlayerIndex)
                              ? Theme::CardSelectedBg
                              : Theme::CardBg);

        if (i == static_cast<std::size_t>(currentPlayerIndex) && players[i]->isAlive()) {
            card.setOutlineThickness(3.f);
            card.setOutlineColor(Theme::BorderGold);
        } else {
            card.setOutlineThickness(2.f);
            card.setOutlineColor(players[i]->isAlive() ? Theme::BorderRed : Theme::DeadMuted);
        }
        window.draw(card);

        sf::CircleShape avatar(34.f);
        avatar.setPosition({x + 46.f, y + 18.f});
        avatar.setFillColor(players[i]->isAlive() ? colors[i] : Theme::DeadMuted);
        window.draw(avatar);

        sf::RectangleShape hpBarBg({120.f, 14.f});
        hpBarBg.setPosition({x + 20.f, y + 125.f});
        hpBarBg.setFillColor(Theme::HpBarBg);
        window.draw(hpBarBg);

        const float hpRatio = static_cast<float>(players[i]->getHealth()) / players[i]->getMaxHealth();
        sf::RectangleShape hpBar({120.f * hpRatio, 14.f});
        hpBar.setPosition({x + 20.f, y + 125.f});
        hpBar.setFillColor(Theme::HpBarFill);
        window.draw(hpBar);

        sf::Text text(font);
        text.setCharacterSize(18);
        text.setFillColor(Theme::TextLight);
        text.setString(players[i]->getName());
        text.setPosition({x + 20.f, y + 160.f});
        window.draw(text);

        text.setCharacterSize(15);
        text.setFillColor(Theme::TextSub);
        text.setString(players[i]->getClassName());
        text.setPosition({x + 20.f, y + 184.f});
        window.draw(text);

        std::ostringstream stats;
        stats << "HP: " << players[i]->getHealth() << "  EN: " << players[i]->getEnergy()
              << "\nPotions: " << players[i]->getPotions();
        text.setCharacterSize(14);
        text.setFillColor(Theme::TextLight);
        text.setString(stats.str());
        text.setPosition({x + 20.f, y + 205.f});
        window.draw(text);
    }
}

void Game::drawBoss() {
    sf::RectangleShape bossPanel({410.f, 300.f});
    bossPanel.setPosition({720.f, 120.f});
    bossPanel.setFillColor(Theme::SubPanelBg);
    bossPanel.setOutlineColor(Theme::BorderDim);
    bossPanel.setOutlineThickness(3.f);
    window.draw(bossPanel);

    sf::CircleShape head(58.f, 6);
    head.setPosition({870.f, 155.f});
    head.setFillColor(boss.isAlive() ? sf::Color(145, 16, 20) : Theme::DeadMuted);
    window.draw(head);

    sf::RectangleShape hpBarBg({300.f, 18.f});
    hpBarBg.setPosition({775.f, 300.f});
    hpBarBg.setFillColor(Theme::HpBarBg);
    window.draw(hpBarBg);

    const float hpRatio = static_cast<float>(boss.getHealth()) / boss.getMaxHealth();
    sf::RectangleShape hpBar({300.f * hpRatio, 18.f});
    hpBar.setPosition({775.f, 300.f});
    hpBar.setFillColor(Theme::HpBarFill);
    window.draw(hpBar);

    sf::Text text(font);
    text.setCharacterSize(26);
    text.setFillColor(Theme::TitleRed);
    text.setString(boss.getName());
    text.setPosition({820.f, 90.f});
    window.draw(text);

    std::ostringstream info;
    info << "HP: " << boss.getHealth() << "/" << boss.getMaxHealth()
         << "\nRage: " << boss.getRage()
         << "\nNext: " << boss.chooseAttackName();
    text.setCharacterSize(18);
    text.setFillColor(Theme::TextLight);
    text.setString(info.str());
    text.setPosition({810.f, 330.f});
    window.draw(text);
}

void Game::drawButtons() {
    for (const auto& button : actionButtons) {
        window.draw(button.shape);
        window.draw(button.label);
    }
}

void Game::drawLog() {
    sf::RectangleShape logPanel({1140.f, 180.f});
    logPanel.setPosition({30.f, 420.f});
    logPanel.setFillColor(Theme::SubPanelBg);
    logPanel.setOutlineColor(Theme::BorderRed);
    logPanel.setOutlineThickness(2.f);
    window.draw(logPanel);

    sf::Text text(font);
    text.setCharacterSize(18);
    text.setFillColor(Theme::TitleRed);
    text.setString("Battle Log");
    text.setPosition({45.f, 430.f});
    window.draw(text);

    text.setCharacterSize(16);
    float y = 455.f;
    for (const auto& line : battleLog) {
        text.setFillColor(Theme::TextLight);
        text.setString("* " + line);
        text.setPosition({50.f, y});
        window.draw(text);
        y += 16.f;
    }
}

void Game::drawStatusBar() {
    sf::Text text(font);
    text.setCharacterSize(16);
    text.setFillColor(Theme::TextSub);
    text.setString("Select your hero!");
    text.setPosition({40.f, 685.f});
    window.draw(text);
}

void Game::drawCenteredText(const std::string& textValue, unsigned int size, sf::Vector2f center, sf::Color color) {
    sf::Text text(font);
    text.setString(textValue);
    text.setCharacterSize(size);
    text.setFillColor(color);

    sf::FloatRect bounds = text.getLocalBounds();
    text.setPosition({
        center.x - bounds.size.x / 2.f - bounds.position.x,
        center.y - bounds.size.y / 2.f - bounds.position.y
    });
    window.draw(text);
}

bool Game::pointInRect(sf::Vector2f point, const sf::FloatRect& rect) const {
    return point.x >= rect.position.x &&
           point.x <= rect.position.x + rect.size.x &&
           point.y >= rect.position.y &&
           point.y <= rect.position.y + rect.size.y;
}
