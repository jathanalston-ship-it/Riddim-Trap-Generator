#include "LibraryPage.h"
#include "../RtgLookAndFeel.h"
#include <algorithm>

namespace rtg::ui {

LibraryPage::LibraryPage(rtg::app::GenerationController& controller)
    : controller_(controller) {
    controller_.addChangeListener(this);

    roleFilter_.addItem("All roles", 1);
    for (int i = 0; i < rtg::kRoleCount; ++i)
        roleFilter_.addItem(rtg::roleName((rtg::Role) i), i + 2);
    roleFilter_.setSelectedId(1, juce::dontSendNotification);
    roleFilter_.onChange = [this] { refresh(); };
    addAndMakeVisible(roleFilter_);

    sortCombo_.addItem("Sort: Score", 1);
    sortCombo_.addItem("Sort: Uses", 2);
    sortCombo_.addItem("Sort: Newest", 3);
    sortCombo_.setSelectedId(1, juce::dontSendNotification);
    sortCombo_.onChange = [this] { refresh(); };
    addAndMakeVisible(sortCombo_);

    auto& header = table_.getHeader();
    header.addColumn("Name", colName, 240, 120, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn("Role", colRole, 110, 80, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn("Score", colScore, 80, 60, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn("Uses", colUses, 70, 50, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn("Fav", colFav, 56, 44, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn("Del", colDelete, 56, 44, -1, juce::TableHeaderComponent::notSortable);
    table_.setRowHeight(30);
    table_.setColour(juce::ListBox::backgroundColourId, Colors::panelBg);
    addAndMakeVisible(table_);

    refresh();
}

LibraryPage::~LibraryPage() {
    controller_.removeChangeListener(this);
}

void LibraryPage::changeListenerCallback(juce::ChangeBroadcaster*) {
    refresh();
}

void LibraryPage::visibilityChanged() {
    if (isVisible()) refresh();
}

void LibraryPage::refresh() {
    rows_ = controller_.library().all();

    if (roleFilter_.getSelectedId() > 1) {
        const auto role = (rtg::Role) (roleFilter_.getSelectedId() - 2);
        rows_.erase(std::remove_if(rows_.begin(), rows_.end(),
                    [role](const rtg::RatedSound& s) { return s.recipe.role != role; }),
                    rows_.end());
    }

    const int sort = sortCombo_.getSelectedId();
    std::stable_sort(rows_.begin(), rows_.end(), [sort](const rtg::RatedSound& a, const rtg::RatedSound& b) {
        if (sort == 2) return a.uses > b.uses;
        if (sort == 3) return a.id > b.id;      // newest ≈ highest id
        return a.score > b.score;                // score default
    });

    table_.updateContent();
    repaint();
}

//==============================================================================
int LibraryPage::getNumRows() { return (int) rows_.size(); }

void LibraryPage::paintRowBackground(juce::Graphics& g, int row, int, int, bool selected) {
    if (selected) g.fillAll(Colors::panelBg2);
    else if (row % 2) g.fillAll(Colors::panelBg.brighter(0.02f));
}

void LibraryPage::paintCell(juce::Graphics& g, int row, int columnId, int w, int h, bool) {
    if (row < 0 || row >= (int) rows_.size()) return;
    const auto& s = rows_[(size_t) row];
    auto* lnf = dynamic_cast<RtgLookAndFeel*>(&getLookAndFeel());
    const juce::Colour accent = lnf ? lnf->accent() : Colors::accentRiddim;
    auto area = juce::Rectangle<int>(0, 0, w, h).reduced(6, 0);

    g.setColour(Colors::text);
    switch (columnId) {
        case colName:
            g.setFont(rtgSansFont(13.0f));
            g.drawText(s.recipe.name.empty() ? juce::String("(unnamed)") : juce::String(s.recipe.name),
                       area, juce::Justification::centredLeft, true);
            break;
        case colRole:
            g.setColour(Colors::dim); g.setFont(rtgSansFont(12.0f));
            g.drawText(rtg::roleName(s.recipe.role), area, juce::Justification::centredLeft, false);
            break;
        case colScore:
            g.setFont(rtgMonoFont(12.0f));
            g.drawText(juce::String(s.score, 2), area, juce::Justification::centredRight, false);
            break;
        case colUses:
            g.setColour(Colors::dim); g.setFont(rtgMonoFont(12.0f));
            g.drawText(juce::String(s.uses), area, juce::Justification::centredRight, false);
            break;
        case colFav:
            g.setColour(s.favorite ? accent : Colors::panelStroke);
            g.setFont(rtgSansFont(16.0f));
            g.drawText(juce::CharPointer_UTF8("\xE2\x98\x85"), area, juce::Justification::centred, false);
            break;
        case colDelete:
            g.setColour(s.favorite ? Colors::panelStroke : Colors::dim);
            g.setFont(rtgSansFont(16.0f));
            g.drawText(juce::CharPointer_UTF8("\xC3\x97"), area, juce::Justification::centred, false);
            break;
        default: break;
    }
}

void LibraryPage::cellClicked(int row, int columnId, const juce::MouseEvent&) {
    if (row < 0 || row >= (int) rows_.size()) return;
    const auto& s = rows_[(size_t) row];
    if (columnId == colFav) {
        controller_.library().setFavorite(s.id, !s.favorite);
        refresh();
    } else if (columnId == colDelete) {
        if (s.favorite) {
            juce::LookAndFeel::getDefaultLookAndFeel().playAlertSound(); // favourites refuse removal
        } else {
            controller_.library().remove(s.id);
            refresh();
        }
    } else {
        controller_.auditionSound(s);
    }
}

void LibraryPage::selectedRowsChanged(int) {}

//==============================================================================
void LibraryPage::resized() {
    auto area = getLocalBounds().reduced(12);
    auto top = area.removeFromTop(30);
    roleFilter_.setBounds(top.removeFromLeft(180));
    top.removeFromLeft(10);
    sortCombo_.setBounds(top.removeFromLeft(160));
    area.removeFromTop(10);
    table_.setBounds(area);
}

void LibraryPage::paint(juce::Graphics& g) {
    g.setColour(Colors::panelBg);
    g.fillRoundedRectangle(table_.getBounds().toFloat(), 8.0f);
    if (rows_.empty()) {
        g.setColour(Colors::dim);
        g.setFont(rtgSansFont(15.0f));
        g.drawText("Library is empty — generate a track to grow it.",
                   table_.getBounds(), juce::Justification::centred, false);
    }
}

} // namespace rtg::ui
