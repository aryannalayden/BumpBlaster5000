import pkgutil

import numpy as np
import pyqtgraph as pg
from pyqtgraph.Qt import QtWidgets, QtGui, QtCore


class WidgetWindow:
    
    def __init__(self):
        self.app = pg.mkQApp("BumpBlaster5000 Interface")
        
        
        self.layout = pg.LayoutWidget()
        self.set_palette()
        
        # push buttons
        self.start_scan_button = QtWidgets.QPushButton("Start Scan")
        self.stop_scan_button = QtWidgets.QPushButton("Stop Scan")
        self.trigger_opto_button = QtWidgets.QPushButton("Trigger Opto Stim")

        self.load_exp_button = QtWidgets.QPushButton("Load Experiment")
        self.run_exp_button = QtWidgets.QPushButton("Run Experiment")
        self.abort_exp_button = QtWidgets.QPushButton("Abort Experiment")

        self.reset_plots_button = QtWidgets.QPushButton("Reset Plots")
        
        

        # checkboxes
        self._exp_label = QtWidgets.QLabel()
        self.exp_combobox = QtWidgets.QComboBox()
        from .. import experiment_protocols
        self.exp_combobox.addItems([mod[1] for mod in pkgutil.iter_modules(experiment_protocols.__path__)])

        self.launch_fictrac_checkbox = QtWidgets.QCheckBox("Launch Fictrac")
        self.send_orientation_checkbox = QtWidgets.QCheckBox("Send Orientation Data")
        
        # text input
        self._heading_pin_label = QtWidgets.QLabel()
        self._heading_pin_label.setText("Set heading pin value:")
        self.heading_pin_input = QtWidgets.QLineEdit()
        self.heading_pin_send_button = QtWidgets.QPushButton("Send heading value [0-4096)")

        self._index_pin_label = QtWidgets.QLabel()
        self._index_pin_label.setText("Set index/y pin value:")
        self.index_pin_input = QtWidgets.QLineEdit()
        self.index_pin_send_button = QtWidgets.QPushButton("Send index/y value [0-4096)")

        
        # plot views
        self.cumm_path_view = pg.widgets.RemoteGraphicsView.RemoteGraphicsView()
        self.cumm_path_view.pg.setConfigOptions(antialias = True)
        self.cumm_path_view.setWindowTitle("Cumulative Path")
        self.app.aboutToQuit.connect(self.cumm_path_view.close)
        self.cumm_path_plotitem = self.cumm_path_view.pg.PlotItem(title="Cumulative Path")
        self.cumm_path_plotitem._setProxyOptions(deferGetattr=True)
        self.cumm_path_view.setCentralItem(self.cumm_path_plotitem)


        self.heading_hist_view = pg.widgets.RemoteGraphicsView.RemoteGraphicsView()
        self.heading_hist_view.pg.setConfigOptions(antialias = True)
        self.heading_hist_view.setWindowTitle("Heading Histogram")
        self.app.aboutToQuit.connect(self.heading_hist_view.close)
        self.heading_hist_plotitem = self.heading_hist_view.pg.PlotItem(title="Heading Histogram")
        self.heading_hist_plotitem._setProxyOptions(deferGetattr=True)
        self.heading_hist_plotitem.setXRange(-1*np.pi/16,2*np.pi + np.pi/16)
        self.heading_hist_view.setCentralItem(self.heading_hist_plotitem)

        
        self.set_layout()
        self.layout.resize(1200,500)
        self.layout.show()

    def set_layout(self):

        # set layout
        self.layout.addWidget(self.start_scan_button, row=0, col=0)
        self.layout.addWidget(self.stop_scan_button, row=0, col=1)
        self.layout.addWidget(self.trigger_opto_button, row=0, col=2)

        self.layout.addWidget(self._exp_label, row=0, col=3, colspan=1)
        self.layout.addWidget(self.exp_combobox, row=0, col=4, colspan=2)
        self.layout.addWidget(self.run_exp_button, row=1, col=4)
        self.layout.addWidget(self.abort_exp_button, row=1, col=5)

        
        self.layout.addWidget(self.launch_fictrac_checkbox, row=1, col=0)
        self.layout.addWidget(self.send_orientation_checkbox, row=1, col=1)
        
        self.layout.addWidget(self.reset_plots_button, row=2, col=0)

        self.layout.addWidget(self._heading_pin_label, row=3, col=0)
        self.layout.addWidget(self.heading_pin_input, row=3, col=1)
        self.layout.addWidget(self.heading_pin_send_button, row=3, col=2)
        
        self.layout.addWidget(self._index_pin_label, row=3, col=3)
        self.layout.addWidget(self.index_pin_input, row=3, col=4)
        self.layout.addWidget(self.index_pin_send_button, row=3, col=5)
        
        self.layout.addWidget(self.cumm_path_view,row=4,col=0,colspan=3,rowspan=3)
        self.layout.addWidget(self.heading_hist_view,row=4,col=3,colspan=3,rowspan=3)
        
        

        for col in range(6):
            self.layout.layout.setColumnStretch(col,1)
        for row in range(6):
            self.layout.layout.setRowStretch(row,1)

    def set_palette(self):

        # set color scheme
        palette = QtGui.QPalette()
        # background
        brush = QtGui.QBrush(QtGui.QColor(36,31,49,255))
        brush.setStyle(QtCore.Qt.SolidPattern)
        palette.setBrush(QtGui.QPalette.Active, QtGui.QPalette.Window,brush)
        palette.setBrush(QtGui.QPalette.Inactive, QtGui.QPalette.Window,brush)
        # window text
        brush = QtGui.QBrush(QtGui.QColor(216, 216, 216))
        brush.setStyle(QtCore.Qt.SolidPattern)
        palette.setBrush(QtGui.QPalette.Inactive, QtGui.QPalette.WindowText, brush)
        palette.setBrush(QtGui.QPalette.Active, QtGui.QPalette.WindowText, brush)
        # push buttons
        brush = QtGui.QBrush(QtGui.QColor(5, 77, 72))
        brush.setStyle(QtCore.Qt.SolidPattern)
        palette.setBrush(QtGui.QPalette.Active, QtGui.QPalette.Button, brush)
        palette.setBrush(QtGui.QPalette.Inactive, QtGui.QPalette.Button, brush)
        brush = QtGui.QBrush(QtGui.QColor(77, 77, 77, 122))
        brush.setStyle(QtCore.Qt.SolidPattern)
        palette.setBrush(QtGui.QPalette.Disabled, QtGui.QPalette.Button, brush)
        
        
        
        self.layout.setPalette(palette)
        
  
    
    

if __name__ == '__main__':
    ww = WidgetWindow()
    pg.exec()
