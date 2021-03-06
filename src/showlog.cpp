/********************************************************************************
 *   This file is part of QSvn Project http://www.anrichter.net/projects/qsvn   *
 *   Copyright (c) 2004-2010 Andreas Richter <ar@anrichter.net>                 *
 *                                                                              *
 *   This program is free software; you can redistribute it and/or modify       *
 *   it under the terms of the GNU General Public License Version 2             *
 *   as published by the Free Software Foundation.                              *
 *                                                                              *
 *   This program is distributed in the hope that it will be useful,            *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 *   GNU General Public License for more details.                               *
 *                                                                              *
 *   You should have received a copy of the GNU General Public License          *
 *   along with this program; if not, write to the                              *
 *   Free Software Foundation, Inc.,                                            *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.                  *
 *                                                                              *
 *******************************************************************************/

//QSvn
#include "config.h"
#include "logentriesmodel.h"
#include "logchangepathentriesmodel.h"
#include "merge.h"
#include "showlog.h"
#include "showlog.moc"
#include "svnclient.h"
#include "statustext.h"
#include "textedit.h"

//svnqt
#include "svnqt/client.hpp"
#include "svnqt/log_entry.hpp"
#include "svnqt/wc.hpp"


//Qt
#include <QtGui>

void ShowLog::doShowLog(QWidget *parent, const QString path,
                        const svn::Revision revisionStart,
                        const svn::Revision revisionEnd )
{
    ShowLog *showLog = new ShowLog(parent, path, revisionStart, revisionEnd);
    showLog->show();
    showLog->raise();
    showLog->activateWindow();
    showLog->on_buttonNext_clicked();
}

ShowLog::ShowLog(QWidget *parent, const QString path,
                 const svn::Revision revisionStart,
                 const svn::Revision revisionEnd)
        : QDialog(0) //don't set parent here! FileSelector is always a top-level window
{
    setAttribute(Qt::WA_DeleteOnClose, true);

    m_revisionStart = revisionStart;
    m_revisionEnd = revisionEnd;
    m_revisionBeginShowLog = revisionStart;
    m_path = QDir::fromNativeSeparators(path);
    m_url = svn::Wc(0).getUrl(path);
    m_repos = svn::Wc(0).getRepos(path);
    m_repos_path = QString(m_url).remove(m_repos);

    setupUi(this);

    Config::instance()->restoreWidget(this);
    Config::instance()->restoreSplitter(this, splitter);

    initLogEntries();
    initLogEntriesPath();
    initMenus();

    this->setWindowTitle(QString(tr("Show Log for %1")).arg(path));
    editFilterString->setFocus(Qt::MouseFocusReason);
}

ShowLog::~ShowLog()
{
    Config::instance()->saveWidget(this);
    Config::instance()->saveSplitter(this, splitter);
    Config::instance()->saveHeaderView(this, viewLogEntries->header());
    Config::instance()->saveHeaderView(this, viewLogChangePathEntries->header());
    Config::instance()->setValue("comboBoxFilterKeyColumn", comboBoxFilterKeyColumn->currentIndex());
}

void ShowLog::initLogEntries()
{
    m_logEntriesModel = new LogEntriesModel(this);
    m_logEntriesProxy = new QSortFilterProxyModel(this);
    m_logEntriesProxy->setDynamicSortFilter(true);
    m_logEntriesProxy->setSourceModel(m_logEntriesModel);
    comboBoxFilterKeyColumn->insertItem(0, tr("All"));
    for (int i = 0; i < m_logEntriesModel->columnCount(QModelIndex()); i++)
        comboBoxFilterKeyColumn->insertItem(comboBoxFilterKeyColumn->count(),
                                            m_logEntriesModel->headerData(i, Qt::Horizontal, Qt::DisplayRole).toString());
    comboBoxFilterKeyColumn->setCurrentIndex(Config::instance()->value("comboBoxFilterKeyColumn").toInt());
    connect(editFilterString, SIGNAL(textChanged(const QString &)),
            m_logEntriesProxy, SLOT(setFilterFixedString(const QString &)));

    viewLogEntries->setModel(m_logEntriesProxy);
    viewLogEntries->installEventFilter(this);
    Config::instance()->restoreHeaderView(this, viewLogEntries->header());
    connect(viewLogEntries->selectionModel(),
            SIGNAL(selectionChanged(const QItemSelection &, const QItemSelection &)),
            this,
            SLOT(selectionChanged(const QItemSelection &, const QItemSelection &)));
}

void ShowLog::initLogEntriesPath()
{
    m_logChangePathEntriesModel = new LogChangePathEntriesModel(this, m_repos_path);
    m_logChangePathEntriesProxy = new QSortFilterProxyModel(this);
    m_logChangePathEntriesProxy->setDynamicSortFilter(true);
    m_logChangePathEntriesProxy->setSourceModel(m_logChangePathEntriesModel);

    viewLogChangePathEntries->setModel(m_logChangePathEntriesProxy);
    viewLogChangePathEntries->installEventFilter(this);
    viewLogChangePathEntries->sortByColumn(1, Qt::AscendingOrder);
    Config::instance()->restoreHeaderView(this, viewLogChangePathEntries->header());
    connect(viewLogChangePathEntries, SIGNAL(doubleClicked(const QModelIndex &)),
            this, SLOT(on_actionDiff_triggered()));
}

void ShowLog::initMenus()
{
    menuLogEntries = new QMenu(this);
    menuLogEntries->addAction(actionMerge);
    menuLogEntries->addAction(actionRevertChangeset);
    menuLogEntries->addAction(actionEditLogMessage);
    menuLogEntries->addAction(actionEditAuthor);

    menuPathEntries = new QMenu(this);
    menuPathEntries->addAction(actionDiff);
    QMenu *menuPathEntriesDiff = menuPathEntries->addMenu(tr("Show differences against..."));
    menuPathEntriesDiff->addAction(actionDiff_to_WORKING);
    menuPathEntriesDiff->addAction(actionDiff_to_HEAD);
    menuPathEntriesDiff->addAction(actionDiff_to_BASE);
    menuPathEntriesDiff->addAction(actionDiff_to_START);
    menuPathEntriesDiff->addAction(actionDiff_to_Revision);
    menuPathEntries->addAction(actionRevertPath);
}

void ShowLog::on_buttonNext_clicked()
{
    buttonNextClicked(100);
}

void ShowLog::on_buttonShowAll_clicked()
{
    buttonNextClicked(0);
}

void ShowLog::buttonNextClicked(int limit)
{
    setEnabled(false);
    qApp->processEvents();
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

    svn::LogEntriesMap _logs;

    if (SvnClient::instance()->log(m_url,
                                m_revisionStart,
                                m_revisionEnd,
                                svn::Revision::HEAD,
                                true,
                                (checkBoxStrictNodeHistory->checkState() == Qt::Checked),
                                limit,_logs)) {
        m_logEntriesModel->appendLogEntries(_logs);

    }
    m_revisionStart = m_logEntriesModel->getLogEntry(
            m_logEntriesModel->index(m_logEntriesModel->rowCount() - 1, 0)).revision;

    buttonNext->setEnabled(m_revisionStart.revnum() > m_revisionEnd.revnum());
    buttonShowAll->setEnabled(m_revisionStart.revnum() > m_revisionEnd.revnum());

    QApplication::restoreOverrideCursor();
    setEnabled(true);
    qApp->processEvents();
}

bool ShowLog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == viewLogChangePathEntries)
    {
        if (event->type() == QEvent::ContextMenu)
        {
            menuPathEntries->popup(static_cast<QContextMenuEvent*>(event)->globalPos());
        }
    } else if (watched == viewLogEntries)
    {
        if (event->type() == QEvent::ContextMenu)
        {
            menuLogEntries->popup(static_cast<QContextMenuEvent*>(event)->globalPos());
        }
    }
    return QDialog::eventFilter(watched, event);
}

void ShowLog::selectionChanged(const QItemSelection &selected,
                               const QItemSelection &deselected)
{
    if (selected.isEmpty())
    {
        editLogMessage->clear();
        m_logChangePathEntriesModel-> setChangePathEntries(svn::LogChangePathEntries());
    } else {
        QModelIndex index = m_logEntriesProxy->mapToSource(selected.indexes().at(0));
        if (index.isValid())
        {
            editLogMessage->setPlainText(m_logEntriesModel->getLogEntry(index).message);
            m_logChangePathEntriesModel->setChangePathEntries(m_logEntriesModel->getLogEntry(index).changedPaths);
        }
    }
}

void ShowLog::on_checkBoxStrictNodeHistory_stateChanged()
{
    viewLogEntries->clearSelection();
    m_logEntriesModel->clear();
    m_revisionStart = m_revisionBeginShowLog;
    on_buttonNext_clicked();
}

svn::Revision ShowLog::getSelectedRevision()
{
    svn::LogEntry logEntry;
    QModelIndexList indexes;
    indexes = viewLogEntries->selectionModel()->selectedIndexes();
    if (indexes.count() == 0)
        return svn::Revision(svn::Revision::UNDEFINED);
    logEntry = m_logEntriesModel->getLogEntry(m_logEntriesProxy->mapToSource(indexes.at(0)));
    return logEntry.revision;
}

QString ShowLog::getSelectedPath()
{
    svn::LogChangePathEntry logChangePathEntry;
    QModelIndexList indexes;
    indexes = viewLogChangePathEntries->selectionModel()->selectedIndexes();
    if (indexes.count() == 0)
        return QString();
    logChangePathEntry = m_logChangePathEntriesModel->getLogChangePathEntry(
            m_logChangePathEntriesProxy->mapToSource(indexes.at(0)));
    return logChangePathEntry.path;
}

svn_revnum_t ShowLog::getSelectedStartRevision()
{
    svn::LogEntriesMap _logs;;
    if (SvnClient::instance()->log(svn::Wc(0).getRepos(m_path) + getSelectedPath(),
                                             getSelectedRevision(),
                                             svn::Revision::START,
                                             svn::Revision::HEAD,
                                             true,
                                             true,
                                             0,
                                             _logs)) {
        _logs.values().first().revision;
    }
    return svn::Revision::UNDEFINED;
}

bool ShowLog::checkLocatedInWc()
{
    if (getSelectedPath().startsWith(m_repos_path))
        return true;
    else
    {
        StatusText::out(tr("This file is not located in the working copy."));
        return false;
    }
}

void ShowLog::on_actionDiff_triggered()
{
    SvnClient::instance()->diff(m_repos + getSelectedPath(),
                                m_repos + getSelectedPath(),
                                svn::Revision(getSelectedRevision().revnum() - 1),
                                getSelectedRevision());
}

void ShowLog::on_actionDiff_to_WORKING_triggered()
{
    if (checkLocatedInWc())
    {
        SvnClient::instance()->diff(m_repos + getSelectedPath(),
                                    m_path + QString(getSelectedPath()).remove(m_repos_path),
                                    getSelectedRevision(),
                                    svn::Revision::WORKING);
    }
}

void ShowLog::on_actionDiff_to_HEAD_triggered()
{
    SvnClient::instance()->diff(m_repos + getSelectedPath(),
                                m_repos + getSelectedPath(),
                                getSelectedRevision(),
                                svn::Revision::HEAD);
}

void ShowLog::on_actionDiff_to_BASE_triggered()
{
    if (checkLocatedInWc())
    {
        SvnClient::instance()->diff(m_repos + getSelectedPath(),
                                    m_path + QString(getSelectedPath()).remove(m_repos_path),
                                    getSelectedRevision(),
                                    svn::Revision::BASE);
    }
}

void ShowLog::on_actionDiff_to_START_triggered()
{
    SvnClient::instance()->diff(m_repos + getSelectedPath(),
                                m_repos + getSelectedPath(),
                                getSelectedRevision(),
                                getSelectedStartRevision());
}

void ShowLog::on_actionDiff_to_Revision_triggered( )
{
    int revision = QInputDialog::getInteger(this,
                                            tr("Revision Number"),
                                            tr("Revision Number"),
                                            getSelectedRevision().revnum() - 1);
    SvnClient::instance()->diff(m_repos + getSelectedPath(),
                                m_repos + getSelectedPath(),
                                getSelectedRevision(),
                                svn::Revision(revision));
}
void ShowLog::on_comboBoxFilterKeyColumn_currentIndexChanged(int index)
{
    m_logEntriesProxy->setFilterKeyColumn(index - 1); //first entry is the All-entry
}

void ShowLog::on_actionMerge_triggered( )
{
    Merge::doMerge(m_url, svn::Revision(getSelectedRevision().revnum() - 1),
                   m_url, getSelectedRevision(),
                   m_path);
}

void ShowLog::on_actionRevertChangeset_triggered()
{
    QString _url = m_url;
    QString _path = m_path;
    svn::Revision _rev = getSelectedRevision();
    revertChanges(_url, _path, _rev);
}

void ShowLog::on_actionRevertPath_triggered()
{
    if (checkLocatedInWc())
    {
        QString _url = m_repos + getSelectedPath();
        QString _path = m_path + QString(getSelectedPath()).remove(m_repos_path);
        svn::Revision _rev = getSelectedRevision();
        revertChanges(_url, _path, _rev);
    }
}

void ShowLog::revertChanges(const QString url, const QString path, const svn::Revision revision)
{
    if (QMessageBox::question(
        this,
        tr("Revert"),
           tr("Do you really want to revert all changes from revision %1 in\n%2?")
                   .arg(revision.revnum())
                   .arg(path),
                        QMessageBox::Yes, QMessageBox::No) == QMessageBox::Yes)
    {
        if (SvnClient::instance()->merge(
            url, revision,
            url, svn::Revision(revision.revnum() - 1),
            path, true, true))
        {
            QMessageBox::information(
                                     this,
                                     tr("Revert"),
                                     tr("All changes from revision %1 successfully reverted in\n%2.")
                                         .arg(revision.revnum())
                                         .arg(path));
        }
    }
}

void ShowLog::on_actionEditLogMessage_triggered()
{
    QString logMessage = editLogMessage->toPlainText();
    if (TextEdit::edit(this, tr("Edit Log Message"), logMessage))
    {
        if (SvnClient::instance()->revPropSet("svn:log", logMessage, m_url, getSelectedRevision()))
        {
            QModelIndex index = viewLogEntries->selectionModel()->currentIndex();
            if (index.isValid())
            {
                m_logEntriesModel->changeLogMessage(m_logEntriesProxy->mapToSource(index), logMessage);
                editLogMessage->setPlainText(logMessage);
            }
        }
        else
        {
            QMessageBox::critical(this, tr("Edit Log Message"),
                                  tr("Can't edit the log message.\n\n%1")
                                          .arg(SvnClient::instance()->getLastErrorMessage()));
        }
    }
}

void ShowLog::on_actionEditAuthor_triggered()
{
    QModelIndex index = viewLogEntries->selectionModel()->currentIndex();
    QString author = m_logEntriesModel->getLogEntry(m_logEntriesProxy->mapToSource(index)).author;
    if (TextEdit::edit(this, tr("Edit Author"), author))
    {
        if (SvnClient::instance()->revPropSet("svn:author", author, m_url, getSelectedRevision()))
        {
            m_logEntriesModel->changeLogAuthor(m_logEntriesProxy->mapToSource(index), author);
        }
        else
        {
            QMessageBox::critical(this, tr("Edit Author"),
                                  tr("Can't edit the author.\n\n%1")
                                          .arg(SvnClient::instance()->getLastErrorMessage()));
        }
    }
}
