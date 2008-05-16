/**********************************************************************
  AlignTool - AlignTool Tool for Avogadro

  Copyright (C) 2008 Marcus D. Hanwell

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.sourceforge.net/>

  Avogadro is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  Avogadro is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
  02110-1301, USA.
 **********************************************************************/

#include "aligntool.h"

#include <avogadro/navigate.h>
#include <avogadro/primitive.h>
#include <avogadro/color.h>
#include <avogadro/glwidget.h>

#include <openbabel/obiter.h>
#include <openbabel/generic.h>

#include <cmath>

#include <QDebug>
#include <QtPlugin>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QHBoxLayout>
#include <QVBoxLayout>

using namespace std;
using namespace OpenBabel;
using namespace Eigen;

namespace Avogadro {

  AlignTool::AlignTool(QObject *parent) : Tool(parent),  m_molecule(0),
  m_numSelectedAtoms(0), m_axis(2), m_settingsWidget(0)
  {
    QAction *action = activateAction();
    action->setIcon(QIcon(QString::fromUtf8(":/align/align.png")));
    action->setToolTip(tr("Align Molecules\n\n"
          "Left Mouse: \tSelect up to two atoms.\n"
          "\tThe first atom is centered at the origin.\n"
          "\tThe second atom is aligned to the selected axis.\n"
          "Right Mouse: \tReset alignment."));
    action->setShortcut(Qt::Key_F12);

    // clear the selected atoms
    int size = m_selectedAtoms.size();
    for(int i=0; i<size; i++)
    {
      m_selectedAtoms[i] = NULL;
    }
  }

  AlignTool::~AlignTool()
  {
  }

  QUndoCommand* AlignTool::mousePress(GLWidget *widget, const QMouseEvent *event)
  {
    m_molecule = widget->molecule();
    if(!m_molecule)
      return 0;

    //! List of hits from initial click
    QList<GLHit> m_hits = widget->hits(event->pos().x()-2, event->pos().y()-2, 5, 5);

    // If there's a left button (and no modifier keys) continue adding to the list
    if(m_hits.size() && (event->buttons() & Qt::LeftButton && event->modifiers() == Qt::NoModifier))
    {
      if(m_hits[0].type() != Primitive::AtomType)
        return 0;

      Atom *atom = (Atom *)m_molecule->GetAtom(m_hits[0].name());

      if(m_numSelectedAtoms < 2)
      {
        // Select another atom
        m_selectedAtoms[m_numSelectedAtoms++] = atom;
        widget->update();
      }
    }
    // Right button or Left Button + modifier (e.g., Mac)
    else
    {
      m_numSelectedAtoms = 0;
      widget->update();
    }
    return 0;
  }

  QUndoCommand* AlignTool::mouseMove(GLWidget*, const QMouseEvent *)
  {
    return 0;
  }

  QUndoCommand* AlignTool::mouseRelease(GLWidget*, const QMouseEvent*)
  {
    return 0;
  }

  QUndoCommand* AlignTool::wheel(GLWidget*widget, const QWheelEvent*event)
  {
    // let's set the reference to be the center of the visible
    // part of the molecule.
    Eigen::Vector3d atomsBarycenter(0., 0., 0.);
    double sumOfWeights = 0.;
    std::vector<OpenBabel::OBNodeBase*>::iterator i;
    for ( Atom *atom = static_cast<Atom*>(widget->molecule()->BeginAtom(i));
          atom; atom = static_cast<Atom*>(widget->molecule()->NextAtom(i))) {
      Eigen::Vector3d transformedAtomPos = widget->camera()->modelview() * atom->pos();
      double atomDistance = transformedAtomPos.norm();
      double dot = transformedAtomPos.z() / atomDistance;
      double weight = exp(-30. * (1. + dot));
      sumOfWeights += weight;
      atomsBarycenter += weight * atom->pos();
    }
    atomsBarycenter /= sumOfWeights;

    Navigate::zoom(widget, atomsBarycenter, - MOUSE_WHEEL_SPEED * event->delta());
    widget->update();

    return NULL;
  }

  bool AlignTool::paint(GLWidget *widget)
  {
    if(m_numSelectedAtoms > 0)
    {
      Vector3d xAxis = widget->camera()->backTransformedXAxis();
      Vector3d zAxis = widget->camera()->backTransformedZAxis();
      // Check the atom is still around...
      if (m_selectedAtoms[0])
      {
        glColor3f(1.0,0.0,0.0);
        widget->painter()->setColor(1.0, 0.0, 0.0);
        Vector3d pos = m_selectedAtoms[0]->pos();

        // relative position of the text on the atom
        double radius = widget->radius(m_selectedAtoms[0]) + 0.05;
        Vector3d textRelPos = radius * (zAxis + xAxis);

        Vector3d textPos = pos+textRelPos;
        widget->painter()->drawText(textPos, "*1");
        widget->painter()->drawSphere(pos, radius);
      }

      if(m_numSelectedAtoms >= 2)
      {
        // Check the atom is still around...
        if (m_selectedAtoms[1])
        {
          glColor3f(0.0,1.0,0.0);
          widget->painter()->setColor(0.0, 1.0, 0.0);
          Vector3d pos = m_selectedAtoms[1]->pos();
          double radius = widget->radius(m_selectedAtoms[1]) + 0.05;
          widget->painter()->drawSphere(pos, radius);
          Vector3d textRelPos = radius * (zAxis + xAxis);
          Vector3d textPos = pos+textRelPos;
          widget->painter()->drawText(textPos, "*2");
        }
      }
    }

    return true;
  }

  void AlignTool::align()
  {
    // Check we have a molecule, otherwise we can't do anything
    if (m_molecule.isNull())
      return;

    QList<Primitive*> neighborList;
    if (m_numSelectedAtoms)
    {
      // Check the first atom still exists, return if not
      if (m_selectedAtoms[0].isNull())
        return;
      
      // We really want the "connected fragment" since a Molecule can contain
      // multiple user visible molecule fragments
      OBMolAtomDFSIter iter(m_molecule, m_selectedAtoms[0]->GetIdx());
      Atom *tmpNeighbor;
      do
      {
        tmpNeighbor = static_cast<Atom*>(&*iter);
        neighborList.append(tmpNeighbor);
      } while ((iter++).next()); // this returns false when we've gone looped through the fragment
    }
    // Align the molecule along the selected axis
    if (m_numSelectedAtoms >= 1)
    {
      // Translate the first selected atom to the origin
      MatrixP3d atomTranslation;
      atomTranslation.loadTranslation(-m_selectedAtoms[0]->pos());
      foreach(Primitive *p, neighborList)
      {
        if (!p) continue;
        Atom *a = static_cast<Atom *>(p);
        a->setPos(atomTranslation * a->pos());
        a->update();
      }
    }
    if (m_numSelectedAtoms >= 2)
    {
      // Check the second atom still exists, return if not
      if (m_selectedAtoms[1].isNull())
        return;
      // Now line up the line from atom[0] to atom[1] with the axis selected
      double alpha, beta, gamma;
      alpha = beta = gamma = 0.0;

      Vector3d pos = m_selectedAtoms[1]->pos();
      pos.normalize();
      Vector3d axis;

      if (m_axis == 0) // x-axis
        axis = Vector3d(1., 0., 0.);
      else if (m_axis == 1) // y-axis
        axis = Vector3d(0., 1., 0.);
      else if (m_axis == 2) // z-axis
        axis = Vector3d(0., 0., 1.);

      // Calculate the angle of the atom from the axis
      double angle = acos(axis.dot(pos));

      // If the angle is zero then we don't need to do anything here
      if (angle > 0)
      {
        // Get the axis for the rotation
        axis = axis.cross(pos);
        axis.normalize();

        // Now to load up the rotation matrix and rotate the molecule
        MatrixP3d atomRotation;
        atomRotation.loadRotation3(-angle, axis);

        // Now to rotate the fragment
        foreach(Primitive *p, neighborList)
        {
          Atom *a = static_cast<Atom *>(p);
          a->setPos(atomRotation * a->pos());
          a->update();
        }
      }
    }
    m_numSelectedAtoms = 0;
  }

  QWidget* AlignTool::settingsWidget()
  {
    if(!m_settingsWidget) {
      m_settingsWidget = new QWidget;

      QLabel *labelAxis = new QLabel(tr("Axis:"));
      labelAxis->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
      labelAxis->setMaximumHeight(15);

      // Small popup with 10 most common elements for organic chemistry
      // (and extra for "other" to bring up periodic table window)
      QComboBox *comboAxis = new QComboBox(m_settingsWidget);
      comboAxis->addItem("x");
      comboAxis->addItem("y");
      comboAxis->addItem("z");
      comboAxis->setCurrentIndex(2);

      QPushButton *buttonAlign = new QPushButton(m_settingsWidget);
      buttonAlign->setText(tr("Align"));
      connect(buttonAlign, SIGNAL(clicked()), this, SLOT(align()));

      QHBoxLayout *hLayout = new QHBoxLayout();
      hLayout->addWidget(labelAxis);
      hLayout->addWidget(comboAxis);
      hLayout->addStretch(1);
      QVBoxLayout *layout = new QVBoxLayout();
      layout->addLayout(hLayout);
      layout->addWidget(buttonAlign);
      layout->addStretch(1);
      m_settingsWidget->setLayout(layout);

      connect(comboAxis, SIGNAL(currentIndexChanged(int)),
              this, SLOT(axisChanged(int)));

      connect(m_settingsWidget, SIGNAL(destroyed()),
              this, SLOT(settingsWidgetDestroyed()));
    }

    return m_settingsWidget;
  }

  void AlignTool::axisChanged(int axis)
  {
    // Axis to use - x=0, y=1, z=2
    m_axis = axis;
  }

  void AlignTool::settingsWidgetDestroyed()
  {
    m_settingsWidget = 0;
  }

}

#include "aligntool.moc"

Q_EXPORT_PLUGIN2(aligntool, Avogadro::AlignToolFactory)
