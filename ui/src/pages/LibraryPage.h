#pragma once
// Library page (doc 09 §3): filter/sort row + TableListBox of rated sounds.
// Row click auditions; ★ toggles favourite; × deletes (favourites refuse).
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include "GenerationController.h"

namespace rtg::ui {

class LibraryPage : public juce::Component,
                    public juce::ChangeListener,
                    public juce::TableListBoxModel {
public:
    explicit LibraryPage(rtg::app::GenerationController& controller);
    ~LibraryPage() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void visibilityChanged() override;

    // TableListBoxModel
    int getNumRows() override;
    void paintRowBackground(juce::Graphics&, int row, int w, int h, bool selected) override;
    void paintCell(juce::Graphics&, int row, int columnId, int w, int h, bool selected) override;
    void cellClicked(int row, int columnId, const juce::MouseEvent&) override;
    void selectedRowsChanged(int lastRow) override;

private:
    enum Column { colName = 1, colRole, colScore, colUses, colFav, colDelete };
    void refresh();

    rtg::app::GenerationController& controller_;
    juce::ComboBox roleFilter_;
    juce::ComboBox sortCombo_;
    juce::TableListBox table_ { "library", this };
    std::vector<rtg::RatedSound> rows_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryPage)
};

} // namespace rtg::ui
