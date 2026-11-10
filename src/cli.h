#pragma once

#include <QStringList>

// Commands that need no display, so an agent or a script can split songs
// and render mixes: stems check, split, mix, and skill install.
namespace Cli {
// True when the arguments name a command rather than the GUI.
bool isCommand(const QStringList &arguments);
int run(const QStringList &arguments);
}
