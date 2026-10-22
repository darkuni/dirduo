#pragma once

#include "core/Job.h"

#include <QDialog>

class QCheckBox;
class QLineEdit;

// "This file already exists": the incoming item and the one in the way side by
// side, with Replace, Replace if Newer, Skip, Rename and Cancel.
class CollisionDialog : public QDialog
{
    Q_OBJECT
public:
    CollisionDialog(const CollisionRequest &request, int remainingHint, QWidget *parent = nullptr);

    CollisionDecision decision() const { return m_decision; }

private:
    QWidget *sideWidget(const QString &heading, const QString &path, bool highlightNewer, bool isNewer);
    void finish(CollisionAction action);

    CollisionRequest m_request;
    CollisionDecision m_decision;
    QLineEdit *m_newName = nullptr;
    QCheckBox *m_applyAll = nullptr;
};
